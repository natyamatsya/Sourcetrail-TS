#ifndef INTERPROCESS_BACKEND_H
#define INTERPROCESS_BACKEND_H

#include "language_packages.h"
#include "InterprocessConcepts.h"

#if USE_CPP_IPC
#include "IpcInterprocessIndexerCommandManager.h"
#include "IpcInterprocessIntermediateStorageManager.h"
#include "IpcInterprocessIndexingStatusManager.h"
#include "IpcSharedMemoryGarbageCollector.h"
#else
#include "InterprocessIndexerCommandManager.h"
#include "InterprocessIntermediateStorageManager.h"
#include "InterprocessIndexingStatusManager.h"
#include "SharedMemoryGarbageCollector.h"
#endif

#if USE_CPP_IPC
using IndexerCommandManagerImpl       = IpcInterprocessIndexerCommandManager;
using IntermediateStorageManagerImpl   = IpcInterprocessIntermediateStorageManager;
using IndexingStatusManagerImpl       = IpcInterprocessIndexingStatusManager;
using GarbageCollectorImpl            = IpcSharedMemoryGarbageCollector;
#else
using IndexerCommandManagerImpl       = InterprocessIndexerCommandManager;
using IntermediateStorageManagerImpl   = InterprocessIntermediateStorageManager;
using IndexingStatusManagerImpl       = InterprocessIndexingStatusManager;
using GarbageCollectorImpl            = SharedMemoryGarbageCollector;
#endif

static_assert(IndexerCommandManagerConcept<IndexerCommandManagerImpl>);
static_assert(IntermediateStorageManagerConcept<IntermediateStorageManagerImpl>);
static_assert(IndexingStatusManagerConcept<IndexingStatusManagerImpl>);
static_assert(GarbageCollectorConcept<GarbageCollectorImpl>);

#endif // INTERPROCESS_BACKEND_H
