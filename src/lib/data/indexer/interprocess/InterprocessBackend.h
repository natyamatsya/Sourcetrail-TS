#ifndef INTERPROCESS_BACKEND_H
#define INTERPROCESS_BACKEND_H

#include "IpcInterprocessIndexerCommandManager.h"
#include "IpcInterprocessIntermediateStorageManager.h"
#include "IpcInterprocessIndexingStatusManager.h"
#include "IpcSharedMemoryGarbageCollector.h"

using IndexerCommandManagerImpl       = IpcInterprocessIndexerCommandManager;
using IntermediateStorageManagerImpl   = IpcInterprocessIntermediateStorageManager;
using IndexingStatusManagerImpl       = IpcInterprocessIndexingStatusManager;
using GarbageCollectorImpl            = IpcSharedMemoryGarbageCollector;

#endif // INTERPROCESS_BACKEND_H
