#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "types.h"
#endif

// These three are GM-attached in the module build (extern "C++" in their headers -- they appear
// in the classic MessageQueue seam's signatures), so their fwd decls must carry the same
// attachment; exported because the defining declarations that follow in the mesh are exported.
#ifdef SRCTRL_MODULE_PURVIEW
export extern "C++" {
#endif
class MessageBase;
class MessageFilter;
class MessageListenerBase;
#ifdef SRCTRL_MODULE_PURVIEW
}
#endif

// MessageQueue's implementation stays a classic TU forever (stdexec pimpl in MessageQueue.cpp).
// In the module build the class therefore lives in the GLOBAL module: a linkage-specification
// attaches its contents to the global module ([module.unit]/7), which keeps every member and
// s_instance ordinary-mangled so the classic definitions link for importers too. Without this,
// the wrapper's instantiated Message<T>::dispatch() bodies reference module-mangled
// MessageQueue@srctrl.messaging symbols that nothing defines.
#ifdef SRCTRL_MODULE_PURVIEW
export extern "C++" {
#endif
class MessageQueue
{
public:
	typedef std::deque<std::shared_ptr<MessageBase>> MessageBufferType;

	// Follows the messages it picks (`follows`) from their dispatch: `started` as
	// one is pushed to the buffer or processed right away, `handled` once all its
	// listeners have run (also when their tasks were dropped unrun, or a filter
	// dropped it). The activation state machine is one.
	struct Observer
	{
		std::function<bool(const MessageBase&)> follows;
		std::function<void(const MessageBase&)> started;
		std::function<void(const MessageBase&)> handled;
	};

	static std::shared_ptr<MessageQueue> getInstance();

	~MessageQueue();

	void registerListener(MessageListenerBase* listener);
	void unregisterListener(MessageListenerBase* listener);

	MessageListenerBase* getListenerById(Id listenerId) const;

	void addMessageFilter(std::shared_ptr<MessageFilter> filter);
	void setObserver(std::shared_ptr<const Observer> observer);

	void pushMessage(std::shared_ptr<MessageBase> message);
	void processMessage(std::shared_ptr<MessageBase> message, bool asNextTask);

	void startMessageLoopThreaded();
	void startMessageLoop();
	void stopMessageLoop();

	bool loopIsRunning() const;
	bool hasMessagesQueued() const;

	//! Whether messages go to the serial queue of their scheduler id (TabQueues.h),
	//! one item each, instead of to their listeners on the loop's thread.
	void setSendMessagesAsTasks(bool sendMessagesAsTasks);
	//! Where a parallel message's listeners run side by side: `run` starts work on a
	//! pool (the app's compute pool, as a sender). Without one they run in a row.
	void setListenerPool(std::function<void(std::function<void()>)> run);

private:
	static std::shared_ptr<MessageQueue> s_instance;

	MessageQueue();
	MessageQueue(const MessageQueue&) = delete;
	void operator=(const MessageQueue&) = delete;

	void processMessages();
	void handOn(std::shared_ptr<MessageBase> message, bool asNextTask, std::shared_ptr<const Observer> followedBy);
	void sendMessage(std::shared_ptr<MessageBase> message);
	void sendMessageAsTask(
		std::shared_ptr<MessageBase> message, bool asNextTask, std::shared_ptr<const Observer> observer) const;
	std::shared_ptr<const Observer> observer() const;
	std::shared_ptr<const Observer> followerOf(const MessageBase& message) const;

	//! Wake the (event-driven) message loop to drain the buffer. Coalesced so
	//! that a burst of pushMessage() calls schedules at most one pending drain.
	void wakeLoop();

	// Event-driven loop backing (stdexec::run_loop + drain coalescing flag), kept
	// behind a pimpl so stdexec stays out of this widely-included header.
	// stdexec::run_loop is single-shot (finish() is terminal), so startMessageLoop()
	// creates a FRESH MessageLoop per session; the worker thread handle lives
	// outside the pimpl so the swap cannot destroy the object owning the running
	// thread.
	struct MessageLoop;
	std::unique_ptr<MessageLoop> m_messageLoop;
	std::thread m_loopThread;

	MessageBufferType m_messageBuffer;
	std::vector<MessageListenerBase*> m_listeners;
	std::vector<std::shared_ptr<MessageFilter>> m_filters;

	size_t m_currentListenerIndex = 0;
	size_t m_listenersLength = 0;

	std::atomic<bool> m_loopIsRunning = false;
	// Messages taken out of the buffer and not yet handed on (to their listeners or,
	// as tasks, to a scheduler). Counted by hasMessagesQueued(): between the pop and
	// the hand-off a message is in neither place.
	std::atomic<int> m_messagesInFlight = 0;

	mutable std::mutex m_messageBufferMutex;
	mutable std::mutex m_listenersMutex;
	mutable std::mutex m_observerMutex;
	std::shared_ptr<const Observer> m_observer;

	bool m_sendMessagesAsTasks = false;
	std::function<void(std::function<void()>)> m_listenerPool;	// set before the loop starts
};
#ifdef SRCTRL_MODULE_PURVIEW
}
#endif

#endif	  // MESSAGE_QUEUE_H
