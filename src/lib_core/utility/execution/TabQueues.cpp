#include "TabQueues.h"

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <exec/timed_thread_scheduler.hpp>

#include "logging.h"

namespace execution
{
namespace
{
struct Queues
{
	std::mutex mutex;
	std::map<TabId, std::shared_ptr<SerialQueue>> byTab;
};

Queues& queues()
{
	static Queues queues;
	return queues;
}

exec::timed_thread_scheduler timer()
{
	// Never destroyed: a wait still pending at exit is not waited for.
	static exec::timed_thread_context* const context = new exec::timed_thread_context();
	return context->get_scheduler();
}
}	 // namespace

std::shared_ptr<SerialQueue> startQueue(TabId tab, std::shared_ptr<Strand> strand)
{
	std::shared_ptr<SerialQueue> queue =
		strand ? SerialQueue::create(std::move(strand)) : SerialQueue::createWithOwnPool();
	std::shared_ptr<SerialQueue> replaced;
	{
		const std::lock_guard<std::mutex> lock(queues().mutex);
		std::shared_ptr<SerialQueue>& slot = queues().byTab[tab];
		replaced = std::exchange(slot, queue);
	}
	if (replaced)
	{
		replaced->stop();
	}
	return queue;
}

std::shared_ptr<SerialQueue> queueOf(TabId tab)
{
	const std::lock_guard<std::mutex> lock(queues().mutex);
	const auto it = queues().byTab.find(tab);
	return it != queues().byTab.end() ? it->second : nullptr;
}

void stopQueue(TabId tab)
{
	std::shared_ptr<SerialQueue> queue;
	{
		const std::lock_guard<std::mutex> lock(queues().mutex);
		const auto it = queues().byTab.find(tab);
		if (it == queues().byTab.end())
		{
			return;
		}
		queue = std::move(it->second);
		queues().byTab.erase(it);
	}
	queue->stop();
}

void stopQueues()
{
	std::vector<std::shared_ptr<SerialQueue>> stopped;
	{
		const std::lock_guard<std::mutex> lock(queues().mutex);
		for (auto& [tab, queue]: queues().byTab)
		{
			stopped.push_back(std::move(queue));
		}
		queues().byTab.clear();
	}
	for (const std::shared_ptr<SerialQueue>& queue: stopped)
	{
		queue->stop();
	}
}

void post(TabId tab, std::function<void()> work, bool front)
{
	if (const std::shared_ptr<SerialQueue> queue = queueOf(tab))
	{
		queue->post(std::move(work), front);
		return;
	}
	LOG_INFO("work posted to tab " + std::to_string(static_cast<std::size_t>(tab)) + " is dropped: it has no queue");
}

void postAfter(TabId tab, std::chrono::milliseconds delay, std::function<void()> work)
{
	stdexec::start_detached(
		exec::schedule_after(timer(), delay) |
		stdexec::then(withCurrentCause([tab, work = std::move(work)]() { post(tab, work); })));
}
}	 // namespace execution
