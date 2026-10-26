#ifndef TASK_INJECT_STORAGE_H
#define TASK_INJECT_STORAGE_H

#include <cstddef>
#include <memory>

#include "Condition.h"
#include "MessageIndexingInterrupted.h"
#include "MessageListener.h"
#include "StdexecPrelude.h"

#include <exec/task.hpp>

class Storage;
class StorageProvider;

//! The (single) storage writer: repeatedly consumes the largest intermediate
//! storage and injects it into the target persistent storage. Event-driven: it
//! waits, holding no thread, until a storage exists or the producers are done -- no
//! polling. Instruments writer-busy time (the serial-commit ceiling; see the
//! concurrency roadmap's B2 gate).
class TaskInjectStorage: public MessageListener<MessageIndexingInterrupted>
{
public:
	~TaskInjectStorage() override;
	TaskInjectStorage(std::shared_ptr<StorageProvider> storageProvider, std::weak_ptr<Storage> target);

	//! Writes until the producers are done and nothing is left (or the target is
	//! gone). Run again, it writes what came since.
	exec::task<void> run();

private:
	void handleMessage(MessageIndexingInterrupted* message) override;

	std::shared_ptr<StorageProvider> m_storageProvider;
	// Notified when the provider's storages change.
	std::shared_ptr<execution::Condition> m_storagesChanged = std::make_shared<execution::Condition>();
	std::size_t m_changeListener = 0;
	std::weak_ptr<Storage> m_target;

	// instrumentation (accumulated across runs; summary logged once)
	size_t m_injectCount = 0;
	size_t m_injectedSourceLocationCount = 0;
	long long m_injectBusyMs = 0;
	bool m_summaryLogged = false;
};

#endif	  // TASK_INJECT_STORAGE_H
