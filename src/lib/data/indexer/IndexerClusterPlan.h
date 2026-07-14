#ifndef INDEXER_CLUSTER_PLAN_H
#define INDEXER_CLUSTER_PLAN_H

#include <string>
#include <vector>

#include "LanguageType.h"

//! One per-source-group indexer cluster (fan-out S3): how many dedicated
//! subprocesses a source group gets. Only C/C++ groups cluster today — the
//! Rust and Swift supervisors stay singular and accept any group.
struct IndexerClusterEntry
{
	std::string sourceGroupId;
	LanguageType language = LanguageType::UNKNOWN;
	size_t commandCount = 0;
	size_t subprocessCount = 0;
};

//! Distributes `budget` subprocesses across the clusters proportional to their
//! commandCount (largest-remainder method): every cluster with commands gets at
//! least one subprocess — even when that exceeds the budget — and never more
//! than it has commands; clusters without commands get none. Deterministic:
//! remainder ties resolve to earlier entries. The clamped total may undershoot
//! the budget when a small cluster caps out; the surplus is deliberately left
//! unassigned (a subprocess above a cluster's command count could never pop
//! anything).
std::vector<IndexerClusterEntry> allocateIndexerSubprocesses(
	std::vector<IndexerClusterEntry> clusters, size_t budget);

#endif	  // INDEXER_CLUSTER_PLAN_H
