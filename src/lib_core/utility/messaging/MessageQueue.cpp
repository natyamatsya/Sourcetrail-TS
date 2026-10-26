// NOT convertible to an importer TU: StdexecPrelude.h breaks clang-scan-deps ("token is not a
// valid binary operator"), and an importing TU must be scanned. Stays fully classic until the
// upstream scanner fix lands.
#include "MessageQueue.h"

#include <algorithm>
#include <thread>

#include "MessageBase.h"
#include "MessageCause.h"
#include "MessageFilter.h"
#include "MessageListenerBase.h"
#include "ScopedFunctor.h"
#include "StdexecPrelude.h"
#include "TabIds.h"
#include "TabQueues.h"
#include "logging.h"

// Event-driven loop backing: a single-threaded stdexec run_loop drained on its
// own worker thread. pushMessage() schedules a drain onto the run_loop (instead
// of the loop polling every 25 ms); stopMessageLoop() finishes the run_loop and
// joins the worker (instead of busy-waiting).
//
// stdexec::run_loop is single-shot: finish() sets a terminal flag that run()
// never resets, so a stopped loop cannot be restarted. startMessageLoop()
// therefore constructs a fresh MessageLoop per session (which also resets
// drainScheduled, so a drain orphaned by a racing stop cannot leak a stale
// "already scheduled" state into the next session).
struct MessageQueue::MessageLoop
{
	stdexec::run_loop runLoop;
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

void MessageQueue::setObserver(std::shared_ptr<const Observer> observer)
{
	const std::lock_guard<std::mutex> lock(m_observerMutex);
	m_observer = std::move(observer);
}

std::shared_ptr<const MessageQueue::Observer> MessageQueue::observer() const
{
	const std::lock_guard<std::mutex> lock(m_observerMutex);
	return m_observer;
}

std::shared_ptr<const MessageQueue::Observer> MessageQueue::followerOf(const MessageBase& message) const
{
	std::shared_ptr<const Observer> followedBy = observer();
	return followedBy && followedBy->follows(message) ? followedBy : nullptr;
}

void MessageQueue::pushMessage(std::shared_ptr<MessageBase> message)
{
	// On behalf of whatever it is dispatched for now: a message the history replays
	// was dispatched before, for something else.
	message->setCause(currentCause());
	CauseWork::begun(message->getCause());
	// Started as it is dispatched: whoever dispatched it sees it going on.
	if (const std::shared_ptr<const Observer> followedBy = followerOf(*message))
	{
		followedBy->started(*message);
	}
	{
		std::lock_guard<std::mutex> lock(m_messageBufferMutex);
		m_messageBuffer.push_back(message);
	}

	wakeLoop();
}

void MessageQueue::processMessage(std::shared_ptr<MessageBase> message, bool asNextTask)
{
	message->setCause(currentCause());
	CauseWork::begun(message->getCause());
	const std::shared_ptr<const Observer> followedBy = followerOf(*message);
	if (followedBy)
	{
		followedBy->started(*message);
	}
	handOn(message, asNextTask, followedBy);
}

void MessageQueue::handOn(
	std::shared_ptr<MessageBase> message, bool asNextTask, std::shared_ptr<const Observer> followedBy)
{
	if (message->isLogged())
	{
		LOG_INFO_BARE("send " + message->str());
	}

	if (m_sendMessagesAsTasks && message->sendAsTask())
	{
		sendMessageAsTask(message, asNextTask, followedBy);
	}
	else
	{
		// Done with, however its listeners end.
		[[maybe_unused]] ScopedFunctor done([&message, &followedBy]() {
			if (followedBy)
			{
				followedBy->handled(*message);
			}
			CauseWork::ended(message->getCause());
		});
		sendMessage(message);
	}
}

void MessageQueue::startMessageLoopThreaded()
{
	if (m_loopIsRunning.load())
	{
		LOG_ERROR("Loop is already running");
		return;
	}

	if (m_loopThread.joinable())
	{
		m_loopThread.join();	// reclaim a previous session's fully-stopped worker
	}

	m_loopThread = std::thread(&MessageQueue::startMessageLoop, this);
}

void MessageQueue::startMessageLoop()
{
	if (m_loopIsRunning.load())
	{
		LOG_ERROR("Loop is already running");
		return;
	}

	// Fresh single-shot run_loop per session (see MessageLoop). Swapping is safe
	// while the flag is false: producers only touch m_messageLoop after seeing
	// m_loopIsRunning == true in wakeLoop().
	m_messageLoop = std::make_unique<MessageLoop>();

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

	if (m_loopThread.joinable())
	{
		m_loopThread.join();
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
	return m_messageBuffer.size() > 0 || m_messagesInFlight.load() > 0;
}

void MessageQueue::setSendMessagesAsTasks(bool sendMessagesAsTasks)
{
	m_sendMessagesAsTasks = sendMessagesAsTasks;
}

void MessageQueue::setListenerPool(std::function<void(std::function<void()>)> run)
{
	m_listenerPool = std::move(run);
}

std::shared_ptr<MessageQueue> MessageQueue::s_instance;

MessageQueue::MessageQueue(): m_messageLoop(std::make_unique<MessageLoop>()) {}

void MessageQueue::processMessages()
{
	while (true)
	{
		std::shared_ptr<MessageBase> message;
		const std::shared_ptr<const Observer> follower = observer();
		std::vector<std::shared_ptr<MessageBase>> dropped;	// the ones a filter dropped
		{
			std::lock_guard<std::mutex> lock(m_messageBufferMutex);

			const MessageBufferType queued = m_messageBuffer;
			for (std::shared_ptr<MessageFilter> filter: m_filters)
			{
				if (!m_messageBuffer.size())
				{
					break;
				}

				filter->filter(&m_messageBuffer);
			}

			if (queued.size() != m_messageBuffer.size())
			{
				for (const std::shared_ptr<MessageBase>& candidate: queued)
				{
					if (std::find(m_messageBuffer.begin(), m_messageBuffer.end(), candidate) == m_messageBuffer.end())
					{
						dropped.push_back(candidate);
					}
				}
			}

			if (m_messageBuffer.size())
			{
				message = m_messageBuffer.front();
				m_messageBuffer.pop_front();
				// Under the buffer lock, so hasMessagesQueued() never sees the message
				// in neither place.
				m_messagesInFlight++;
			}
		}

		// Done with: handled, as far as their observer goes (started when pushed).
		for (const std::shared_ptr<MessageBase>& queued: dropped)
		{
			if (follower && follower->follows(*queued))
			{
				follower->handled(*queued);
			}
			CauseWork::ended(queued->getCause());
		}

		if (!message)
		{
			break;
		}

		[[maybe_unused]] ScopedFunctor handedOn([this]() { m_messagesInFlight--; });
		// Started when it was pushed.
		handOn(message, false, follower && follower->follows(*message) ? follower : nullptr);
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
			{
				// What the listener dispatches carries the message's cause on.
				const CauseScope cause(message->getCause());
				listener->handleMessageBase(message.get());
			}
			m_listenersMutex.lock();
		}
	}
}

namespace
{
void runListener(MessageBase& message, Id listenerId)
{
	if (MessageListenerBase* listener = MessageQueue::getInstance()->getListenerById(listenerId))
	{
		const CauseScope cause(message.getCause());
		listener->handleMessageBase(&message);
	}
}

// A message's listeners: in a row, or, for a parallel message with a pool to run on,
// side by side there. `ended` once the last of them did, however they ended.
void runListeners(
	const std::shared_ptr<MessageBase>& message,
	const std::vector<Id>& listenerIds,
	const std::function<void(std::function<void()>)>& pool,
	std::function<void()> ended)
{
	if (!message->isParallel() || !pool || listenerIds.size() < 2)
	{
		[[maybe_unused]] ScopedFunctor finished(ended);
		for (const Id listenerId: listenerIds)
		{
			runListener(*message, listenerId);
		}
		return;
	}
	const auto running = std::make_shared<std::atomic<std::size_t>>(listenerIds.size());
	for (const Id listenerId: listenerIds)
	{
		pool([message, listenerId, running, ended]() {
			try
			{
				runListener(*message, listenerId);
			}
			catch (const std::exception& e)
			{
				LOG_ERROR(std::string("a message listener failed: ") + e.what());
			}
			catch (...)
			{
				LOG_ERROR("a message listener failed with an unknown exception");
			}
			if (--*running == 0)
			{
				ended();
			}
		});
	}
}
}	 // namespace

void MessageQueue::sendMessageAsTask(
	std::shared_ptr<MessageBase> message, bool asNextTask, std::shared_ptr<const Observer> observer) const
{
	std::vector<Id> listenerIds;
	{
		std::lock_guard<std::mutex> lock(m_listenersMutex);
		for (MessageListenerBase* listener: m_listeners)
		{
			if (listener->getType() == message->getType() &&
				(message->getSchedulerId() == TabId::NONE || listener->getSchedulerId() == TabId::NONE ||
				 listener->getSchedulerId() == message->getSchedulerId()))
			{
				listenerIds.push_back(listener->getId());
			}
		}
	}

	// Once its listeners ran, or it was dropped unrun.
	std::function<void()> handled = [observer, message]() {
		if (observer)
		{
			observer->handled(*message);
		}
		CauseWork::ended(message->getCause());
	};

	TabId schedulerId = message->getSchedulerId();
	if (schedulerId == TabId::NONE)
	{
		schedulerId = TabIds::app();
	}

	const std::shared_ptr<execution::SerialQueue> queue = execution::queueOf(schedulerId);
	if (!queue)
	{
		handled();	// its tab closed: nobody listens there any more
		return;
	}
	// One item of the queue, in its turn: the next message waits until the listeners
	// ran, also while a parallel message's run on the pool. Queued on behalf of the
	// message's cause: it counts as that request's work.
	const CauseScope cause(message->getCause());
	queue->push(
		{[message, listenerIds = std::move(listenerIds), pool = m_listenerPool, handled](std::function<void()> done) {
			 runListeners(message, listenerIds, pool, [handled, done]() {
				 handled();
				 done();
			 });
		 },
		 handled},
		asNextTask);
}
