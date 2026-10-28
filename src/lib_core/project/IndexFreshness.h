#ifndef INDEX_FRESHNESS_H
#define INDEX_FRESHNESS_H

#include <cstddef>
#include <mutex>
#include <set>

#include "FilePath.h"

// The indexed files that changed on disk since they were indexed, as the last check found
// them (RefreshInfoGenerator::getChangedFilePaths, the rule an update uses). The app
// checks when the window comes to the front and after activations; the status bar, the
// Refresh button and the code files' titles show what it found.
class IndexFreshness
{
public:
	static IndexFreshness& instance();

	// What a check found; true when it differs from what was known.
	bool update(std::set<FilePath> changedFiles);
	void clear();

	std::size_t changedFileCount() const;
	bool isChanged(const FilePath& filePath) const;

private:
	mutable std::mutex m_mutex;
	std::set<FilePath> m_changedFiles;
};

#endif	  // INDEX_FRESHNESS_H
