#include "SharedQueuePool.h"

#include <algorithm>
#include <utility>

namespace execution
{
SharedQueuePool::SharedQueuePool(unsigned threadCount)
{
	const unsigned count = std::max(threadCount, 1u);
	m_threads.reserve(count);
	for (unsigned i = 0; i < count; i++)
	{
		m_threads.emplace_back([this]() { work(); });
	}
}

SharedQueuePool::~SharedQueuePool()
{
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		m_stopping = true;
	}
	m_wake.notify_all();
	for (std::thread& thread: m_threads)
	{
		thread.join();
	}
	// What no thread got to: its receivers learn that it will not run.
	Task* task = std::exchange(m_head, nullptr);
	m_tail = nullptr;
	while (task != nullptr)
	{
		Task* const next = task->next;
		task->stop(task);
		task = next;
	}
}

SharedQueuePool::Scheduler SharedQueuePool::scheduler() noexcept
{
	return Scheduler {this};
}

void SharedQueuePool::push(Task* task)
{
	task->next = nullptr;
	{
		const std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_stopping)
		{
			(m_tail != nullptr ? m_tail->next : m_head) = task;
			m_tail = task;
			m_wake.notify_one();
			return;
		}
	}
	task->stop(task);	// the pool is going away: it will not run
}

void SharedQueuePool::work()
{
	while (true)
	{
		Task* task = nullptr;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_wake.wait(lock, [this]() { return m_stopping || m_head != nullptr; });
			if (m_stopping)
			{
				return;	 // the destructor stops what is left
			}
			task = m_head;
			m_head = task->next;
			if (m_head == nullptr)
			{
				m_tail = nullptr;
			}
		}
		task->run(task);
	}
}
}	 // namespace execution
