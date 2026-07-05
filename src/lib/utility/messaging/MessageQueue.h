#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "types.h"

class MessageBase;
class MessageFilter;
class MessageListenerBase;

class MessageQueue
{
public:
	typedef std::deque<std::shared_ptr<MessageBase>> MessageBufferType;

	static std::shared_ptr<MessageQueue> getInstance();

	~MessageQueue();

	void registerListener(MessageListenerBase* listener);
	void unregisterListener(MessageListenerBase* listener);

	MessageListenerBase* getListenerById(Id listenerId) const;

	void addMessageFilter(std::shared_ptr<MessageFilter> filter);

	void pushMessage(std::shared_ptr<MessageBase> message);
	void processMessage(std::shared_ptr<MessageBase> message, bool asNextTask);

	void startMessageLoopThreaded();
	void startMessageLoop();
	void stopMessageLoop();

	bool loopIsRunning() const;
	bool hasMessagesQueued() const;

	void setSendMessagesAsTasks(bool sendMessagesAsTasks);

private:
	static std::shared_ptr<MessageQueue> s_instance;

	MessageQueue();
	MessageQueue(const MessageQueue&) = delete;
	void operator=(const MessageQueue&) = delete;

	void processMessages();
	void sendMessage(std::shared_ptr<MessageBase> message);
	void sendMessageAsTask(std::shared_ptr<MessageBase> message, bool asNextTask) const;

	//! Wake the (event-driven) message loop to drain the buffer. Coalesced so
	//! that a burst of pushMessage() calls schedules at most one pending drain.
	void wakeLoop();

	// Event-driven loop backing (exec::run_loop + its worker thread), kept behind
	// a pimpl so stdexec stays out of this widely-included header.
	struct MessageLoop;
	std::unique_ptr<MessageLoop> m_messageLoop;

	MessageBufferType m_messageBuffer;
	std::vector<MessageListenerBase*> m_listeners;
	std::vector<std::shared_ptr<MessageFilter>> m_filters;

	size_t m_currentListenerIndex = 0;
	size_t m_listenersLength = 0;

	std::atomic<bool> m_loopIsRunning = false;

	mutable std::mutex m_messageBufferMutex;
	mutable std::mutex m_listenersMutex;

	bool m_sendMessagesAsTasks = false;
};

#endif	  // MESSAGE_QUEUE_H
