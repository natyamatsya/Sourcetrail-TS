// Inline member definitions for SourceLocationCollection.h (included at the end of that header). All
// members are inline because an out-of-line member of an exported class does not resolve for module
// importers. LOG_ERROR comes from logging.h (#included in the header build, imported in the module
// build); SourceLocationFile / SourceLocation are complete via SourceLocationFile.h.

#pragma once

inline SourceLocationCollection::SourceLocationCollection() = default;

inline SourceLocationCollection::~SourceLocationCollection() = default;

inline const std::map<FilePath, std::shared_ptr<SourceLocationFile>>& SourceLocationCollection::
	getSourceLocationFiles() const
{
	return m_files;
}

inline size_t SourceLocationCollection::getSourceLocationCount() const
{
	size_t count = 0;
	for (const auto& p: m_files)
	{
		count += p.second->getSourceLocationCount();
	}
	return count;
}

inline size_t SourceLocationCollection::getSourceLocationFileCount() const
{
	return m_files.size();
}

inline std::shared_ptr<SourceLocationFile> SourceLocationCollection::getSourceLocationFileByPath(
	const FilePath& filePath) const
{
	std::map<FilePath, std::shared_ptr<SourceLocationFile>>::const_iterator it = m_files.find(
		filePath);
	if (it != m_files.end())
	{
		return it->second;
	}

	return nullptr;
}

inline SourceLocation* SourceLocationCollection::getSourceLocationById(Id locationId) const
{
	for (const auto& p: m_files)
	{
		SourceLocation* location = p.second->getSourceLocationById(locationId);
		if (location)
		{
			return location;
		}
	}

	return nullptr;
}

inline SourceLocation* SourceLocationCollection::addSourceLocation(
	LocationType type,
	Id locationId,
	std::vector<Id> tokenIds,
	const FilePath& filePath,
	size_t startLineNumber,
	size_t startColumnNumber,
	size_t endLineNumber,
	size_t endColumnNumber)
{
	if (startLineNumber > endLineNumber ||
		(startLineNumber == endLineNumber && startColumnNumber > endColumnNumber))
	{
		LOG_ERROR(
			"SourceLocation has wrong boundaries: " + filePath.str() + " " +
			std::to_string(startLineNumber) + ":" + std::to_string(startColumnNumber) + " " +
			std::to_string(endLineNumber) + ":" + std::to_string(endColumnNumber));
		return nullptr;
	}

	SourceLocationFile* file = createSourceLocationFile(filePath);
	if (file->isWhole())
	{
		return nullptr;
	}

	return file->addSourceLocation(
		type, locationId, tokenIds, startLineNumber, startColumnNumber, endLineNumber, endColumnNumber);
}

inline SourceLocation* SourceLocationCollection::addSourceLocationCopy(const SourceLocation* location)
{
	SourceLocationFile* other = location->getSourceLocationFile();
	SourceLocationFile* file = createSourceLocationFile(
		location->getFilePath(),
		other->getLanguage(),
		other->isWhole(),
		other->isComplete(),
		other->isIndexed());
	return file->addSourceLocationCopy(location);
}

inline void SourceLocationCollection::addSourceLocationCopies(const SourceLocationCollection* other)
{
	other->forEachSourceLocationFile([this](std::shared_ptr<SourceLocationFile> otherFile) {
		addSourceLocationCopies(otherFile.get());
	});
}

inline void SourceLocationCollection::addSourceLocationCopies(const SourceLocationFile* otherFile)
{
	SourceLocationFile* file = createSourceLocationFile(
		otherFile->getFilePath(),
		otherFile->getLanguage(),
		otherFile->isWhole(),
		otherFile->isComplete(),
		otherFile->isIndexed());

	otherFile->forEachSourceLocation(
		[file](SourceLocation* otherLocation) { file->addSourceLocationCopy(otherLocation); });
}

inline void SourceLocationCollection::addSourceLocationFile(std::shared_ptr<SourceLocationFile> file)
{
	m_files.emplace(file->getFilePath(), file);
}

inline void SourceLocationCollection::forEachSourceLocationFile(
	std::function<void(std::shared_ptr<SourceLocationFile>)> func) const
{
	for (const auto& p: m_files)
	{
		func(p.second);
	}
}

inline void SourceLocationCollection::forEachSourceLocation(std::function<void(SourceLocation*)> func) const
{
	for (const auto& p: m_files)
	{
		p.second->forEachSourceLocation(func);
	}
}

inline SourceLocationFile* SourceLocationCollection::createSourceLocationFile(
	const FilePath& filePath, const std::string& language, bool isWhole, bool isComplete, bool isIndexed)
{
	SourceLocationFile* file = getSourceLocationFileByPath(filePath).get();
	if (file)
	{
		return file;
	}

	std::shared_ptr<SourceLocationFile> filePtr = std::make_shared<SourceLocationFile>(
		filePath, language, isWhole, isComplete, isIndexed);
	m_files.emplace(filePath, filePtr);
	return filePtr.get();
}

inline std::ostream& operator<<(std::ostream& ostream, const SourceLocationCollection& base)
{
	ostream << "Locations:\n";
	base.forEachSourceLocationFile(
		[&ostream](std::shared_ptr<SourceLocationFile> f) { ostream << *f; });
	return ostream;
}
