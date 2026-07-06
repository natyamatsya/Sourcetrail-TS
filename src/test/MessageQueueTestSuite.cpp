#include "Catch2.hpp"

#include <chrono>
#include <thread>

#include "Message.h"
#include "MessageListener.h"
#include "MessageQueue.h"

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
