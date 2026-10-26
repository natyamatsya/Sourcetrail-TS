#include "Catch2.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <set>
#include <thread>

// classic headers (entities not exported): stay textual
#include "MessageCause.h"
#include "MessageQueue.h"
#include "TabQueues.h"
#include "StdexecPrelude.h"
#include "Strand.h"
#include "TabIds.h"
#include "TaskManager.h"
#include "TaskScheduler.h"
#ifndef SRCTRL_MODULE_BUILD
#include "Message.h"
#include "MessageListener.h"
#endif

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
#endif

namespace
{
class TestMessage: public Message<TestMessage>
{
public:
	static const std::string getStaticType()
	{
		return "TestMessage";
	}
};

class Test2Message: public Message<Test2Message>
{
public:
	static const std::string getStaticType()
	{
		return "TestMessage2";
	}
};

class TestMessageListener: public MessageListener<TestMessage>
{
public:
	TestMessageListener() = default;

	int m_messageCount = 0;

private:
	void handleMessage(TestMessage*  /*message*/) override
	{
		m_messageCount++;
	}
};

class Test2MessageListener: public MessageListener<Test2Message>
{
public:
	Test2MessageListener() = default;

	int m_messageCount = 0;

private:
	void handleMessage(Test2Message*  /*message*/) override
	{
		m_messageCount++;
		TestMessage().dispatch();
	}
};

class Test3MessageListener: public MessageListener<Test2Message>
{
public:
	std::shared_ptr<TestMessageListener> m_listener;

private:
	void handleMessage(Test2Message*  /*message*/) override
	{
		m_listener = std::make_shared<TestMessageListener>();
	}
};

class Test4MessageListener
	: public MessageListener<TestMessage>
	, public MessageListener<Test2Message>
{
public:
	std::shared_ptr<TestMessageListener> m_listener;

private:
	void handleMessage(TestMessage*  /*message*/) override
	{
		if (!m_listener)
		{
			m_listener = std::make_shared<TestMessageListener>();
		}
	}

	void handleMessage(Test2Message*  /*message*/) override
	{
		m_listener.reset();
	}
};

class Test5MessageListener: public MessageListener<TestMessage>
{
public:
	std::vector<std::shared_ptr<TestMessageListener>> m_listeners;

private:
	void handleMessage(TestMessage*  /*message*/) override
	{
		if (!m_listeners.size())
		{
			for (size_t i = 0; i < 5; i++)
			{
				m_listeners.push_back(std::make_shared<TestMessageListener>());
			}
		}
	}
};

void waitForThread()
{
	static const int THREAD_WAIT_TIME_MS = 20;
	do
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(THREAD_WAIT_TIME_MS));
	} while (MessageQueue::getInstance()->hasMessagesQueued());
}
}	 // namespace

TEST_CASE("message loop starts and stops")
{
	REQUIRE(!MessageQueue::getInstance()->loopIsRunning());

	MessageQueue::getInstance()->startMessageLoopThreaded();

	waitForThread();

	REQUIRE(MessageQueue::getInstance()->loopIsRunning());

	MessageQueue::getInstance()->stopMessageLoop();

	waitForThread();

	REQUIRE(!MessageQueue::getInstance()->loopIsRunning());
}

TEST_CASE("registered listener receives messages")
{
	MessageQueue::getInstance()->startMessageLoopThreaded();

	TestMessageListener listener;
	Test2MessageListener listener2;

	TestMessage().dispatch();
	TestMessage().dispatch();
	TestMessage().dispatch();

	waitForThread();

	MessageQueue::getInstance()->stopMessageLoop();

	REQUIRE(3 == listener.m_messageCount);
	REQUIRE(0 == listener2.m_messageCount);
}

TEST_CASE("message dispatching within message handling")
{
	MessageQueue::getInstance()->startMessageLoopThreaded();

	TestMessageListener listener;
	Test2MessageListener listener2;

	Test2Message().dispatch();

	waitForThread();

	MessageQueue::getInstance()->stopMessageLoop();

	REQUIRE(1 == listener.m_messageCount);
	REQUIRE(1 == listener2.m_messageCount);
}

TEST_CASE("listener registration within message handling")
{
	MessageQueue::getInstance()->startMessageLoopThreaded();

	Test3MessageListener listener;

	Test2Message().dispatch();
	TestMessage().dispatch();

	waitForThread();

	MessageQueue::getInstance()->stopMessageLoop();

	REQUIRE(listener.m_listener);
	if (listener.m_listener)
	{
		REQUIRE(1 == listener.m_listener->m_messageCount);
	}
}

TEST_CASE("listener unregistration within message handling")
{
	MessageQueue::getInstance()->startMessageLoopThreaded();

	Test4MessageListener listener;

	TestMessage().dispatch();

	Test2Message().dispatch();

	TestMessage().dispatch();
	TestMessage().dispatch();
	TestMessage().dispatch();

	waitForThread();

	MessageQueue::getInstance()->stopMessageLoop();

	REQUIRE(listener.m_listener);
	if (listener.m_listener)
	{
		REQUIRE(2 == listener.m_listener->m_messageCount);
	}
}

TEST_CASE("listener registration to front and back within message handling")
{
	MessageQueue::getInstance()->startMessageLoopThreaded();

	Test5MessageListener listener;

	TestMessage().dispatch();
	TestMessage().dispatch();
	TestMessage().dispatch();

	waitForThread();

	MessageQueue::getInstance()->stopMessageLoop();

	REQUIRE(5 == listener.m_listeners.size());
	REQUIRE(2 == listener.m_listeners[0]->m_messageCount);
	REQUIRE(2 == listener.m_listeners[1]->m_messageCount);
	REQUIRE(2 == listener.m_listeners[2]->m_messageCount);
	REQUIRE(2 == listener.m_listeners[3]->m_messageCount);
	REQUIRE(2 == listener.m_listeners[4]->m_messageCount);
}

// Regression tests for the event-driven message loop (stdexec::run_loop):
// pushMessage() schedules a coalesced drain instead of the old 25 ms poll.

TEST_CASE("messages dispatched before loop start are delivered at start")
{
	TestMessageListener listener;

	// No loop running: dispatch only buffers (no wake is scheduled).
	TestMessage().dispatch();
	TestMessage().dispatch();
	TestMessage().dispatch();

	REQUIRE(MessageQueue::getInstance()->hasMessagesQueued());
	REQUIRE(0 == listener.m_messageCount);

	// startMessageLoop() must drain the pre-existing buffer before blocking.
	MessageQueue::getInstance()->startMessageLoopThreaded();

	waitForThread();

	MessageQueue::getInstance()->stopMessageLoop();

	REQUIRE(3 == listener.m_messageCount);
}

TEST_CASE("concurrent burst dispatch is fully delivered despite coalesced wakeups")
{
	MessageQueue::getInstance()->startMessageLoopThreaded();

	TestMessageListener listener;

	// Two producers race pushMessage() against the drain; the coalescing flag
	// (at most one pending drain) must never drop a message pushed mid-drain.
	constexpr int MESSAGES_PER_PRODUCER = 50;
	std::thread producerA([]() {
		for (int i = 0; i < MESSAGES_PER_PRODUCER; i++)
		{
			TestMessage().dispatch();
		}
	});
	std::thread producerB([]() {
		for (int i = 0; i < MESSAGES_PER_PRODUCER; i++)
		{
			TestMessage().dispatch();
		}
	});
	producerA.join();
	producerB.join();

	waitForThread();

	MessageQueue::getInstance()->stopMessageLoop();

	REQUIRE(2 * MESSAGES_PER_PRODUCER == listener.m_messageCount);
}

namespace
{
class CauseMessage: public Message<CauseMessage>
{
public:
	static const std::string getStaticType()
	{
		return "CauseMessage";
	}
};

class FollowUpMessage: public Message<FollowUpMessage>
{
public:
	static const std::string getStaticType()
	{
		return "FollowUpMessage";
	}
};

// Dispatches a follow-up from its handler, as an activation does.
class CauseListener: public MessageListener<CauseMessage>
{
private:
	void handleMessage(CauseMessage*  /*message*/) override
	{
		FollowUpMessage().dispatch();
	}
};

class FollowUpListener: public MessageListener<FollowUpMessage>
{
public:
	std::atomic<std::uint64_t> m_cause = 1;	// not 0: tells "not run" from "no cause"
	std::atomic<int> m_count = 0;

private:
	void handleMessage(FollowUpMessage* message) override
	{
		m_cause = message->getCause() == currentCause() ? currentCause() : 999;
		m_count++;
	}
};
}	 // namespace

TEST_CASE("a message's cause carries on to what its handling dispatches")
{
	MessageQueue::getInstance()->startMessageLoopThreaded();
	CauseListener causeListener;
	FollowUpListener followUp;

	{
		const CauseScope agentRequest(42);
		CauseMessage().dispatch();
	}
	waitForThread();
	REQUIRE(followUp.m_count == 1);
	REQUIRE(followUp.m_cause == 42);

	CauseMessage().dispatch();	// the user: no cause
	waitForThread();
	MessageQueue::getInstance()->stopMessageLoop();
	REQUIRE(followUp.m_count == 2);
	REQUIRE(followUp.m_cause == 0);
	REQUIRE(currentCause() == 0);
}

TEST_CASE("a request's work is counted until it is done")
{
	std::vector<std::uint64_t> done;
	CauseWork::onDone([&](std::uint64_t cause) { done.push_back(cause); });

	CauseWork::begun(7);
	CauseWork::begun(7);
	CauseWork::begun(0);	// nothing: not counted
	REQUIRE(CauseWork::pending(7) == 2);
	REQUIRE(CauseWork::pending(0) == 0);
	CauseWork::ended(7);
	REQUIRE(done.empty());
	CauseWork::ended(7);
	REQUIRE(CauseWork::pending(7) == 0);
	REQUIRE(done == std::vector<std::uint64_t> {7});
	CauseWork::ended(7);	// more ends than begins: ignored
	REQUIRE(done.size() == 1);

	{
		const CauseScope request(8);
		CauseWork::Held held;	// the current cause's
		CauseWork::Held moved = std::move(held);	// one piece of work, moved
		REQUIRE(CauseWork::pending(8) == 1);
	}
	REQUIRE(CauseWork::pending(8) == 0);
	REQUIRE(done == std::vector<std::uint64_t> {7, 8});

	CauseWork::onDone(nullptr);
}

namespace
{
class ParallelMessage: public Message<ParallelMessage>
{
public:
	ParallelMessage()
	{
		setIsParallel(true);
	}
	static const std::string getStaticType()
	{
		return "ParallelMessage";
	}
};

// Two of these handle a ParallelMessage side by side; the next message waits.
class SlowListener: public MessageListener<ParallelMessage>
{
public:
	explicit SlowListener(std::atomic<int>& running, std::atomic<int>& most): m_running(running), m_most(most) {}

	std::atomic<int> handled = 0;

private:
	void handleMessage(ParallelMessage*  /*message*/) override
	{
		const int now = ++m_running;
		for (int most = m_most; now > most && !m_most.compare_exchange_weak(most, now);)
		{
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(80));
		--m_running;
		handled++;
	}

	std::atomic<int>& m_running;
	std::atomic<int>& m_most;
};

class AfterListener: public MessageListener<Test2Message>
{
public:
	explicit AfterListener(std::atomic<int>& running): m_running(running) {}

	std::atomic<int> ranAlongside = 0;
	std::atomic<int> handled = 0;

private:
	void handleMessage(Test2Message*  /*message*/) override
	{
		ranAlongside += m_running.load();
		handled++;
	}

	std::atomic<int>& m_running;
};
}	 // namespace

TEST_CASE("a parallel message's listeners run side by side on the pool, and the next one waits")
{
	// Two pools, as the app has (I/O for the strands, compute for the listeners): a
	// static_thread_pool keeps work scheduled from one of its threads on that thread.
	exec::static_thread_pool io(2);
	exec::static_thread_pool compute(2);
	const auto strand = execution::Strand::create(io.get_scheduler());
	execution::startQueue(TabIds::app(), strand);
	MessageQueue* queue = MessageQueue::getInstance().get();
	queue->setSendMessagesAsTasks(true);
	queue->setListenerPool([scheduler = compute.get_scheduler()](std::function<void()> work) {
		stdexec::start_detached(stdexec::schedule(scheduler) | stdexec::then(std::move(work)));
	});
	queue->startMessageLoopThreaded();

	std::atomic<int> running = 0;
	std::atomic<int> most = 0;
	SlowListener first(running, most);
	SlowListener second(running, most);
	AfterListener after(running);

	{
		const CauseScope request(77);
		ParallelMessage().dispatch();
		Test2Message().dispatch();
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (after.handled == 0 && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	while (CauseWork::pending(77) > 0 && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	queue->stopMessageLoop();
	queue->setListenerPool(nullptr);
	queue->setSendMessagesAsTasks(false);
	execution::stopQueue(TabIds::app());

	REQUIRE(first.handled == 1);
	REQUIRE(second.handled == 1);
	REQUIRE(most == 2);	// side by side
	REQUIRE(after.handled == 1);
	REQUIRE(after.ranAlongside == 0);	// after them, not alongside
	REQUIRE(CauseWork::pending(77) == 0);	// all of the request's work is done
}
