#ifndef TASK_INJECT_STORAGE_H
#define TASK_INJECT_STORAGE_H

#include <cstddef>
#include <memory>

#include "MessageIndexingInterrupted.h"
#include "MessageListener.h"
#include "Task.h"

class Storage;
class StorageProvider;

//! The (single) storage writer: repeatedly consumes the largest intermediate
//! storage and injects it into the target persistent storage. Event-driven:
//! blocks on the provider until a storage exists or producers are done -- no
//! polling. Instruments writer-busy time (the serial-commit ceiling; see the
//! concurrency roadmap's B2 gate).
class TaskInjectStorage
	: public Task
	, public MessageListener<MessageIndexingInterrupted>
{
public:
	TaskInjectStorage(std::shared_ptr<StorageProvider> storageProvider, std::weak_ptr<Storage> target);

private:
	void doEnter(std::shared_ptr<Blackboard> blackboard) override;
	TaskState doUpdate(std::shared_ptr<Blackboard> blackboard) override;
	void doExit(std::shared_ptr<Blackboard> blackboard) override;
	void doReset(std::shared_ptr<Blackboard> blackboard) override;
	void terminate() override;

	void handleMessage(MessageIndexingInterrupted* message) override;

	std::shared_ptr<StorageProvider> m_storageProvider;
	std::weak_ptr<Storage> m_target;

	// instrumentation (accumulated across repeat iterations; summary logged once)
	size_t m_injectCount = 0;
	size_t m_injectedSourceLocationCount = 0;
	long long m_injectBusyMs = 0;
	bool m_summaryLogged = false;
};

#endif	  // TASK_INJECT_STORAGE_H
