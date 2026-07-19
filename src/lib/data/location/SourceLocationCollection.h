#ifndef SOURCE_LOCATION_COLLECTION_H
#define SOURCE_LOCATION_COLLECTION_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <vector>

#include "LocationType.h"
#include "SourceLocationFile.h"
#include "logging.h"
#include "types.h"
#endif

#ifndef SRCTRL_MODULE_PURVIEW
class FilePath;
#endif
SRCTRL_EXPORT class SourceLocation;
SRCTRL_EXPORT class SourceLocationFile;

SRCTRL_EXPORT class SourceLocationCollection
{
public:
	SourceLocationCollection();
	virtual ~SourceLocationCollection();

	const std::map<FilePath, std::shared_ptr<SourceLocationFile>>& getSourceLocationFiles() const;

	std::size_t getSourceLocationCount() const;
	std::size_t getSourceLocationFileCount() const;

	std::shared_ptr<SourceLocationFile> getSourceLocationFileByPath(const FilePath& filePath) const;
	SourceLocation* getSourceLocationById(Id locationId) const;

	SourceLocation* addSourceLocation(
		LocationType type,
		Id locationId,
		std::vector<Id> tokenIds,
		const FilePath& filePath,
		std::size_t startLineNumber,
		std::size_t startColumnNumber,
		std::size_t endLineNumber,
		std::size_t endColumnNumber);

	SourceLocation* addSourceLocationCopy(const SourceLocation* location);
	void addSourceLocationCopies(const SourceLocationCollection* other);
	void addSourceLocationCopies(const SourceLocationFile* otherFile);

	void addSourceLocationFile(std::shared_ptr<SourceLocationFile> file);

	void forEachSourceLocationFile(std::function<void(std::shared_ptr<SourceLocationFile>)> func) const;
	void forEachSourceLocation(std::function<void(SourceLocation*)> func) const;

private:
	SourceLocationFile* createSourceLocationFile(
		const FilePath& filePath,
		const std::string& language = "",
		bool isWhole = false,
		bool isComplete = false,
		bool isIndexed = false);

	std::map<FilePath, std::shared_ptr<SourceLocationFile>> m_files;
};

std::ostream& operator<<(std::ostream& ostream, const SourceLocationCollection& base);

// In a module build the wrapper includes the .inl explicitly AFTER all three class defs (the
// SourceLocation<->SourceLocationFile cycle needs both complete), so guard it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "SourceLocationCollection.inl"
#endif

#endif	  // SOURCE_LOCATION_COLLECTION_H
