#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <map>
#include <memory>
#include <mutex>

#include "TabIds.h"
#include "TaskScheduler.h"

class TaskManager
{
public:
	static std::shared_ptr<TaskScheduler> createScheduler(TabId schedulerId);
	static void destroyScheduler(TabId schedulerId);
	
	static std::shared_ptr<TaskScheduler> getScheduler(TabId schedulerId);

private:
	static std::map<TabId, std::shared_ptr<TaskScheduler>> s_schedulers;
	static std::mutex s_schedulersMutex;
};

#endif	  // TASK_MANAGER_H
