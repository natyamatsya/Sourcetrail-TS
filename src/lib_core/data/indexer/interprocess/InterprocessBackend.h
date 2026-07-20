#ifndef INTERPROCESS_BACKEND_H
#define INTERPROCESS_BACKEND_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "IpcInterprocessIndexerCommandManager.h"
#include "IpcInterprocessIntermediateStorageManager.h"
#include "IpcInterprocessIndexingStatusManager.h"
#include "IpcSharedMemoryGarbageCollector.h"
#endif

SRCTRL_EXPORT using IndexerCommandManagerImpl       = IpcInterprocessIndexerCommandManager;
SRCTRL_EXPORT using IntermediateStorageManagerImpl  = IpcInterprocessIntermediateStorageManager;
SRCTRL_EXPORT using IndexingStatusManagerImpl       = IpcInterprocessIndexingStatusManager;
SRCTRL_EXPORT using GarbageCollectorImpl            = IpcSharedMemoryGarbageCollector;

#endif // INTERPROCESS_BACKEND_H
