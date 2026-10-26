#include "TaskParking.h"

#include <thread>
#include <utility>

#include "ScopedFunctor.h"
#include "WorkPools.h"

namespace
{
// The runner whose task the calling thread updates now, if any, and how its driver
// wakes it once it parked.
struct CurrentTask
{
	std::shared_ptr<TaskRunner> runner;
	std::function<void()> wake;
};
thread_local CurrentTask* t_currentTask = nullptr;
}	 // namespace

std::function<void()> TaskParking::parkCurrentTask()
{
	if (t_currentTask == nullptr || !t_currentTask->runner || !t_currentTask->wake)
	{
		return {};
	}
	t_currentTask->runner->park();
	return t_currentTask->wake;
}

void TaskParking::waitForNextLook()
{
	std::this_thread::sleep_for(unparkedLookInterval);
}

void TaskParking::restFor(std::chrono::milliseconds delay)
{
	if (std::function<void()> wake = parkCurrentTask())
	{
		execution::afterDelay(delay, std::move(wake));
	}
	else
	{
		std::this_thread::sleep_for(delay);
	}
}

Task::TaskState TaskParking::updateDriven(
	const std::shared_ptr<TaskRunner>& runner, std::function<void()> wake, const std::shared_ptr<Blackboard>& blackboard)
{
	CurrentTask current {runner, std::move(wake)};
	CurrentTask* const outer = std::exchange(t_currentTask, &current);
	[[maybe_unused]] ScopedFunctor restore([outer]() { t_currentTask = outer; });
	return runner->update(blackboard);
}
