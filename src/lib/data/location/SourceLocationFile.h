#ifndef SOURCE_LOCATION_FILE_H
#define SOURCE_LOCATION_FILE_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <set>

#include "FilePath.h"
#include "LocationType.h"
#include "types.h"
#endif

#ifndef SRCTRL_MODULE_PURVIEW
class FilePath;
#endif
SRCTRL_EXPORT class SourceLocation;

SRCTRL_EXPORT class SourceLocationFile
{
public:
	struct LocationComp
	{
		bool operator()(
			const std::shared_ptr<SourceLocation>& lhs, const std::shared_ptr<SourceLocation>& rhs) const;
	};

	SourceLocationFile(
		const FilePath& filePath,
		const std::string& language,
		bool isWhole,
		bool isComplete,
		bool isIndexed);
	virtual ~SourceLocationFile();

	const FilePath& getFilePath() const;

	void setLanguage(const std::string& language);
	const std::string& getLanguage() const;

	void setIsWhole(bool isWhole);
	bool isWhole() const;

	void setIsComplete(bool isComplete);
	bool isComplete() const;

	void setIsIndexed(bool isIndexed);
	bool isIndexed() const;

	const std::multiset<std::shared_ptr<SourceLocation>, LocationComp>& getSourceLocations() const;

	std::size_t getSourceLocationCount() const;
	std::size_t getUnscopedStartLocationCount() const;

	SourceLocation* addSourceLocation(
		LocationType type,
		Id locationId,
		std::vector<Id> tokenIds,
		std::size_t startLineNumber,
		std::size_t startColumnNumber,
		std::size_t endLineNumber,
		std::size_t endColumnNumber);
	SourceLocation* addSourceLocationCopy(const SourceLocation* location);

	void copySourceLocations(std::shared_ptr<SourceLocationFile> file);

	SourceLocation* getSourceLocationById(Id locationId) const;

	void forEachSourceLocation(std::function<void(SourceLocation*)> func) const;
	void forEachStartSourceLocation(std::function<void(SourceLocation*)> func) const;
	void forEachEndSourceLocation(std::function<void(SourceLocation*)> func) const;

	std::shared_ptr<SourceLocationFile> getFilteredByLines(
		std::size_t firstLineNumber, std::size_t lastLineNumber) const;
	std::shared_ptr<SourceLocationFile> getFilteredByType(LocationType type) const;
	std::shared_ptr<SourceLocationFile> getFilteredByTypes(const std::vector<LocationType>& types) const;

private:
	const FilePath m_filePath;
	std::string m_language;
	bool m_isWhole;
	bool m_isComplete;
	bool m_isIndexed;

	std::multiset<std::shared_ptr<SourceLocation>, LocationComp> m_locations;
	std::map<Id, SourceLocation*> m_locationIndex;
};

std::ostream& operator<<(std::ostream& ostream, const SourceLocationFile& base);

// SourceLocationFile's members need the complete SourceLocation type, so pull it in before the .inl.
// SourceLocation <-> SourceLocationFile is a mutual dependency: this include sits AFTER the class
// definition (which only needs SourceLocation forward-declared, since LocationComp::operator() moved to
// the .inl) so the include cycle terminates.
#ifndef SRCTRL_MODULE_PURVIEW
#include "SourceLocation.h"
#endif

// In a module build the wrapper includes the .inl explicitly AFTER all three class defs (the
// SourceLocation<->SourceLocationFile cycle needs both complete), so guard it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "SourceLocationFile.inl"
#endif

#endif	  // SOURCE_LOCATION_FILE_H
