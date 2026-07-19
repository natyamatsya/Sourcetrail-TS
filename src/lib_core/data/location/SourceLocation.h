#ifndef SOURCE_LOCATION_H
#define SOURCE_LOCATION_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <ostream>
#include <string>
#include <vector>

#include "LocationType.h"
#include "types.h"
#endif

#ifndef SRCTRL_MODULE_PURVIEW
class FilePath;
#endif
SRCTRL_EXPORT class SourceLocationFile;

SRCTRL_EXPORT class SourceLocation
{
public:
	SourceLocation(
		SourceLocationFile* file,
		LocationType type,
		Id locationId,
		std::vector<Id> tokenIds,
		std::size_t lineNumber,
		std::size_t columnNumber,
		bool isStart);
	SourceLocation(SourceLocation* other, std::size_t lineNumber, std::size_t columnNumber);
	SourceLocation(const SourceLocation* other, SourceLocationFile* file);
	virtual ~SourceLocation();

	bool operator==(const SourceLocation& rhs) const;
	bool operator<(const SourceLocation& rhs) const;
	bool operator>(const SourceLocation& rhs) const;

	bool contains(const SourceLocation& other) const;

	SourceLocationFile* getSourceLocationFile() const;

	Id getLocationId() const;
	const std::vector<Id>& getTokenIds() const;
	LocationType getType() const;

	std::size_t getColumnNumber() const;
	std::size_t getLineNumber() const;
	const FilePath& getFilePath() const;

	const SourceLocation* getOtherLocation() const;
	void setOtherLocation(SourceLocation* other);

	const SourceLocation* getStartLocation() const;
	const SourceLocation* getEndLocation() const;

	bool isStartLocation() const;
	bool isEndLocation() const;

	bool isScopeLocation() const;

private:
	SourceLocationFile* m_file;

	LocationType m_type;

	const Id m_locationId;
	const std::vector<Id> m_tokenIds;

	const std::size_t m_lineNumber;
	const std::size_t m_columnNumber;

	SourceLocation* m_other;
	const bool m_isStart;
};

std::ostream& operator<<(std::ostream& ostream, const SourceLocation& location);

// getFilePath()'s inline body dereferences SourceLocationFile, so the complete type must precede the
// .inl. SourceLocation <-> SourceLocationFile is a mutual dependency: this include sits AFTER the class
// definition (not in the top block) so that when SourceLocationFile.h re-enters SourceLocation.h the
// class above is already complete, breaking the include cycle.
#ifndef SRCTRL_MODULE_PURVIEW
#include "SourceLocationFile.h"
#endif

// In a module build the wrapper includes the .inl explicitly AFTER all three class defs (the
// SourceLocation<->SourceLocationFile cycle needs both complete), so guard it here.
#ifndef SRCTRL_MODULE_PURVIEW
#include "SourceLocation.inl"
#endif

#endif	  // SOURCE_LOCATION_H
