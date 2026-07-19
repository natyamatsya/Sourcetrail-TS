#include "TaskManager.h"

#include "TaskScheduler.h"

std::map<TabId, std::shared_ptr<TaskScheduler>> TaskManager::s_schedulers;
std::mutex TaskManager::s_schedulersMutex;

std::shared_ptr<TaskScheduler> TaskManager::createScheduler(TabId schedulerId)
{
	return getScheduler(schedulerId);
}

void TaskManager::destroyScheduler(TabId schedulerId)
{
	std::lock_guard<std::mutex> lock(s_schedulersMutex);

	auto it = s_schedulers.find(schedulerId);
	if (it != s_schedulers.end())
	{
		s_schedulers.erase(it);
	}
}

std::shared_ptr<TaskScheduler> TaskManager::getScheduler(TabId schedulerId)
{
	std::lock_guard<std::mutex> lock(s_schedulersMutex);

	auto it = s_schedulers.find(schedulerId);
	if (it != s_schedulers.end())
	{
		return it->second;
	}

	std::shared_ptr<TaskScheduler> scheduler = std::make_shared<TaskScheduler>(schedulerId);
	s_schedulers.emplace(schedulerId, scheduler);
	return scheduler;
}
