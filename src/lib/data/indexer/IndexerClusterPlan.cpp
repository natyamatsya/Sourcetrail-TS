#include "IndexerClusterPlan.h"

#include <algorithm>

std::vector<IndexerClusterEntry> allocateIndexerSubprocesses(
	std::vector<IndexerClusterEntry> clusters, size_t budget)
{
	size_t totalCommands = 0;
	size_t nonEmptyClusters = 0;
	for (IndexerClusterEntry& cluster: clusters)
	{
		cluster.subprocessCount = cluster.commandCount > 0 ? 1 : 0;
		totalCommands += cluster.commandCount;
		if (cluster.commandCount > 0)
			nonEmptyClusters++;
	}

	if (nonEmptyClusters == 0 || budget <= nonEmptyClusters)
		return clusters;

	// Distribute the budget beyond the min-1 guarantee by largest remainder.
	const size_t rest = budget - nonEmptyClusters;
	struct Remainder
	{
		size_t index;
		double fraction;
	};
	std::vector<Remainder> remainders;
	remainders.reserve(nonEmptyClusters);

	size_t assigned = 0;
	for (size_t i = 0; i < clusters.size(); i++)
	{
		if (clusters[i].commandCount == 0)
			continue;

		const double exact =
			static_cast<double>(rest) * static_cast<double>(clusters[i].commandCount) /
			static_cast<double>(totalCommands);
		const size_t whole = static_cast<size_t>(exact);
		clusters[i].subprocessCount += whole;
		assigned += whole;
		remainders.push_back({i, exact - static_cast<double>(whole)});
	}

	std::stable_sort(remainders.begin(), remainders.end(), [](const Remainder& a, const Remainder& b) {
		return a.fraction > b.fraction;
	});
	for (size_t k = 0; k < remainders.size() && assigned < rest; k++, assigned++)
	{
		clusters[remainders[k].index].subprocessCount++;
	}

	// More subprocesses than commands in a cluster can never all find work.
	for (IndexerClusterEntry& cluster: clusters)
	{
		cluster.subprocessCount = std::min(cluster.subprocessCount, cluster.commandCount);
	}

	return clusters;
}
