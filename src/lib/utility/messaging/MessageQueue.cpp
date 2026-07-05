#include "MessageQueue.h"

#include <thread>

#include "MessageBase.h"
#include "MessageFilter.h"
#include "MessageListenerBase.h"
#include "StdexecPrelude.h"
#include "TabIds.h"
#include "TaskGroupParallel.h"
#include "TaskGroupSequence.h"
#include "TaskLambda.h"
#include "logging.h"

// Event-driven loop backing: a single-threaded stdexec run_loop drained on its
// own worker thread. pushMessage() schedules a drain onto the run_loop (instead
// of the loop polling every 25 ms); stopMessageLoop() finishes the run_loop and
// joins the worker (instead of busy-waiting).
struct MessageQueue::MessageLoop
{
	stdexec::run_loop runLoop;
	std::thread thread;
	std::atomic<bool> drainScheduled = false;
};

std::shared_ptr<MessageQueue> MessageQueue::getInstance()
{
	if (!s_instance)
	{
		s_instance = std::shared_ptr<MessageQueue>(new MessageQueue());
	}
	return s_instance;
}

MessageQueue::~MessageQueue()
{
	stopMessageLoop();

	std::lock_guard<std::mutex> lock(m_listenersMutex);
	for (size_t i = 0; i < m_listeners.size(); i++)
	{
		m_listeners[i]->removedListener();
	}
	m_listeners.clear();
}

void MessageQueue::registerListener(MessageListenerBase* listener)
{
	std::lock_guard<std::mutex> lock(m_listenersMutex);
	m_listeners.push_back(listener);
}

void MessageQueue::unregisterListener(MessageListenerBase* listener)
{
	std::lock_guard<std::mutex> lock(m_listenersMutex);
	for (size_t i = 0; i < m_listeners.size(); i++)
	{
		if (m_listeners[i] == listener)
		{
			m_listeners.erase(m_listeners.begin() + i);

			// m_currentListenerIndex and m_listenersLength need to be updated in case this happens
			// while a message is handled.
			if (i <= m_currentListenerIndex)
			{
				m_currentListenerIndex--;
			}

			if (i < m_listenersLength)
			{
				m_listenersLength--;
			}

			return;
		}
	}

	LOG_ERROR("Listener was not found");
}

MessageListenerBase* MessageQueue::getListenerById(Id listenerId) const
{
	std::lock_guard<std::mutex> lock(m_listenersMutex);
	for (size_t i = 0; i < m_listeners.size(); i++)
	{
		if (m_listeners[i]->getId() == listenerId)
		{
			return m_listeners[i];
		}
	}
	return nullptr;
}

void MessageQueue::addMessageFilter(std::shared_ptr<MessageFilter> filter)
{
	m_filters.push_back(filter);
}

void MessageQueue::pushMessage(std::shared_ptr<MessageBase> message)
{
	{
		std::lock_guard<std::mutex> lock(m_messageBufferMutex);
		m_messageBuffer.push_back(message);
	}

	wakeLoop();
}

void MessageQueue::processMessage(std::shared_ptr<MessageBase> message, bool asNextTask)
{
	if (message->isLogged())
	{
		LOG_INFO_BARE("send " + message->str());
	}

	if (m_sendMessagesAsTasks && message->sendAsTask())
	{
		sendMessageAsTask(message, asNextTask);
	}
	else
	{
		sendMessage(message);
	}
}

void MessageQueue::startMessageLoopThreaded()
{
	m_messageLoop->thread = std::thread(&MessageQueue::startMessageLoop, this);
}

void MessageQueue::startMessageLoop()
{
	if (m_loopIsRunning.exchange(true))
	{
		LOG_ERROR("Loop is already running");
		return;
	}

	// Drain anything enqueued before the loop started, then block in the
	// run_loop processing scheduled drains until stopMessageLoop() finishes it.
	processMessages();
	m_messageLoop->runLoop.run();
}

void MessageQueue::stopMessageLoop()
{
	if (!m_loopIsRunning.exchange(false))
	{
		LOG_WARNING("Loop is not running");
		return;
	}

	m_messageLoop->runLoop.finish();

	if (m_messageLoop->thread.joinable())
	{
		m_messageLoop->thread.join();
	}
}

void MessageQueue::wakeLoop()
{
	// Only wake once the loop is running (otherwise the message stays buffered
	// and startMessageLoop()'s initial drain picks it up). Coalesce: if a drain
	// is already pending it will process this message too, so schedule at most
	// one. drainScheduled is reset at the start of the drain, so a message that
	// arrives mid-drain schedules a fresh one.
	if (!m_loopIsRunning.load() || m_messageLoop->drainScheduled.exchange(true))
	{
		return;
	}

	stdexec::start_detached(
		stdexec::schedule(m_messageLoop->runLoop.get_scheduler()) |
		stdexec::then(
			[this]() noexcept
			{
				m_messageLoop->drainScheduled.store(false);
				try
				{
					processMessages();
				}
				catch (const std::exception& e)
				{
					LOG_ERROR(std::string("message handling threw: ") + e.what());
				}
				catch (...)
				{
					LOG_ERROR("message handling threw an unknown exception");
				}
			}));
}

bool MessageQueue::loopIsRunning() const
{
	return m_loopIsRunning.load();
}

bool MessageQueue::hasMessagesQueued() const
{
	std::lock_guard<std::mutex> lock(m_messageBufferMutex);
	return m_messageBuffer.size() > 0;
}

void MessageQueue::setSendMessagesAsTasks(bool sendMessagesAsTasks)
{
	m_sendMessagesAsTasks = sendMessagesAsTasks;
}

std::shared_ptr<MessageQueue> MessageQueue::s_instance;

MessageQueue::MessageQueue(): m_messageLoop(std::make_unique<MessageLoop>()) {}

void MessageQueue::processMessages()
{
	while (true)
	{
		std::shared_ptr<MessageBase> message;
		{
			std::lock_guard<std::mutex> lock(m_messageBufferMutex);

			for (std::shared_ptr<MessageFilter> filter: m_filters)
			{
				if (!m_messageBuffer.size())
				{
					break;
				}

				filter->filter(&m_messageBuffer);
			}

			if (!m_messageBuffer.size())
			{
				break;
			}

			message = m_messageBuffer.front();
			m_messageBuffer.pop_front();
		}

		processMessage(message, false);
	}
}

void MessageQueue::sendMessage(std::shared_ptr<MessageBase> message)
{
	std::lock_guard<std::mutex> lock(m_listenersMutex);

	// m_listenersLength is saved, so that new listeners registered within message handling don't
	// get the current message and the length can be reduced when a listener gets unregistered.
	m_listenersLength = m_listeners.size();

	// The currentListenerIndex holds the index of the current listener being handled, so it can be
	// changed when a listener gets removed while message handling.
	for (m_currentListenerIndex = 0; m_currentListenerIndex < m_listenersLength;
		 m_currentListenerIndex++)
	{
		MessageListenerBase* listener = m_listeners[m_currentListenerIndex];

		if (listener->getType() == message->getType() &&
			(message->getSchedulerId() == TabId::NONE || listener->getSchedulerId() == TabId::NONE ||
			 listener->getSchedulerId() == message->getSchedulerId()))
		{
			// The listenersMutex gets unlocked so changes to listeners are possible while message handling.
			m_listenersMutex.unlock();
			listener->handleMessageBase(message.get());
			m_listenersMutex.lock();
		}
	}
}

void MessageQueue::sendMessageAsTask(std::shared_ptr<MessageBase> message, bool asNextTask) const
{
	std::shared_ptr<TaskGroup> taskGroup;
	if (message->isParallel())
	{
		taskGroup = std::make_shared<TaskGroupParallel>();
	}
	else
	{
		taskGroup = std::make_shared<TaskGroupSequence>();
	}

	{
		std::lock_guard<std::mutex> lock(m_listenersMutex);
		for (size_t i = 0; i < m_listeners.size(); i++)
		{
			MessageListenerBase* listener = m_listeners[i];

			if (listener->getType() == message->getType() &&
				(message->getSchedulerId() == TabId::NONE || listener->getSchedulerId() == TabId::NONE ||
				 listener->getSchedulerId() == message->getSchedulerId()))
			{
				Id listenerId = listener->getId();
				taskGroup->addTask(std::make_shared<TaskLambda>([listenerId, message]() {
					MessageListenerBase* listener = MessageQueue::getInstance()->getListenerById(
						listenerId);
					if (listener)
					{
						listener->handleMessageBase(message.get());
					}
				}));
			}
		}
	}

	TabId schedulerId = message->getSchedulerId();
	if (schedulerId == TabId::NONE)
	{
		schedulerId = TabIds::app();
	}

	if (asNextTask)
	{
		Task::dispatchNext(schedulerId, taskGroup);
	}
	else
	{
		Task::dispatch(schedulerId, taskGroup);
	}
}
