#include "IndexFreshness.h"

IndexFreshness& IndexFreshness::instance()
{
	static IndexFreshness freshness;
	return freshness;
}

bool IndexFreshness::update(std::set<FilePath> changedFiles)
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	if (m_changedFiles == changedFiles)
	{
		return false;
	}
	m_changedFiles = std::move(changedFiles);
	return true;
}

void IndexFreshness::clear()
{
	update({});
}

std::size_t IndexFreshness::changedFileCount() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	return m_changedFiles.size();
}

bool IndexFreshness::isChanged(const FilePath& filePath) const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	return m_changedFiles.contains(filePath);
}
