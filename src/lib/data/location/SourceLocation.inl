// Inline member definitions for SourceLocation.h (included at the end of that header). All members are
// inline because an out-of-line member of an exported class does not resolve for module importers.
// getFilePath() forwards to SourceLocationFile, so the header pulls in SourceLocationFile.h (classic
// build) / imports it (module build) before this .inl.

#pragma once

inline SourceLocation::SourceLocation(
	SourceLocationFile* file,
	LocationType type,
	Id locationId,
	std::vector<Id> tokenIds,
	std::size_t lineNumber,
	std::size_t columnNumber,
	bool isStart)
	: m_file(file)
	, m_type(type)
	, m_locationId(locationId)
	, m_tokenIds(tokenIds)
	, m_lineNumber(lineNumber)
	, m_columnNumber(columnNumber)
	, m_other(nullptr)
	, m_isStart(isStart)
{
}

inline SourceLocation::SourceLocation(SourceLocation* other, std::size_t lineNumber, std::size_t columnNumber)
	: m_file(other->m_file)
	, m_type(other->m_type)
	, m_locationId(other->m_locationId)
	, m_tokenIds(other->m_tokenIds)
	, m_lineNumber(lineNumber)
	, m_columnNumber(columnNumber)
	, m_other(other)
	, m_isStart(!other->m_isStart)
{
	other->setOtherLocation(this);
}

inline SourceLocation::SourceLocation(const SourceLocation* other, SourceLocationFile* file)
	: m_file(file)
	, m_type(other->m_type)
	, m_locationId(other->m_locationId)
	, m_tokenIds(other->m_tokenIds)
	, m_lineNumber(other->m_lineNumber)
	, m_columnNumber(other->m_columnNumber)
	, m_other(nullptr)
	, m_isStart(other->m_isStart)
{
}

inline SourceLocation::~SourceLocation() = default;

inline bool SourceLocation::operator==(const SourceLocation& rhs) const
{
	return (
		getLineNumber() == rhs.getLineNumber() && getColumnNumber() == rhs.getColumnNumber() &&
		getLocationId() == rhs.getLocationId() && getType() == rhs.getType());
}

inline bool SourceLocation::operator<(const SourceLocation& rhs) const
{
	if (getLineNumber() != rhs.getLineNumber())
	{
		return getLineNumber() < rhs.getLineNumber();
	}

	if (getColumnNumber() != rhs.getColumnNumber())
	{
		return getColumnNumber() < rhs.getColumnNumber();
	}

	return getLocationId() < rhs.getLocationId();
}

inline bool SourceLocation::operator>(const SourceLocation& rhs) const
{
	if (getLineNumber() != rhs.getLineNumber())
	{
		return getLineNumber() > rhs.getLineNumber();
	}

	if (getColumnNumber() != rhs.getColumnNumber())
	{
		return getColumnNumber() > rhs.getColumnNumber();
	}

	return getLocationId() > rhs.getLocationId();
}

inline bool SourceLocation::contains(const SourceLocation& other) const
{
	const SourceLocation* start = getStartLocation();
	const SourceLocation* otherStart = other.getStartLocation();

	if (start->getLineNumber() > otherStart->getLineNumber())
	{
		return false;
	}

	if (start->getLineNumber() == otherStart->getLineNumber() &&
		start->getColumnNumber() > otherStart->getColumnNumber())
	{
		return false;
	}

	const SourceLocation* end = getEndLocation();
	const SourceLocation* otherEnd = other.getEndLocation();

	if (end->getLineNumber() < otherEnd->getLineNumber())
	{
		return false;
	}

	if (end->getLineNumber() == otherEnd->getLineNumber() &&
		end->getColumnNumber() < otherEnd->getColumnNumber())
	{
		return false;
	}

	return true;
}

inline SourceLocationFile* SourceLocation::getSourceLocationFile() const
{
	return m_file;
}

inline Id SourceLocation::getLocationId() const
{
	return m_locationId;
}

inline const std::vector<Id>& SourceLocation::getTokenIds() const
{
	return m_tokenIds;
}

inline LocationType SourceLocation::getType() const
{
	return m_type;
}

inline std::size_t SourceLocation::getColumnNumber() const
{
	return m_columnNumber;
}

inline std::size_t SourceLocation::getLineNumber() const
{
	return m_lineNumber;
}

inline const FilePath& SourceLocation::getFilePath() const
{
	return m_file->getFilePath();
}

inline const SourceLocation* SourceLocation::getOtherLocation() const
{
	return m_other;
}

inline void SourceLocation::setOtherLocation(SourceLocation* other)
{
	m_other = other;
}

inline const SourceLocation* SourceLocation::getStartLocation() const
{
	if (m_isStart)
	{
		return this;
	}
	else
	{
		return m_other;
	}
}

inline const SourceLocation* SourceLocation::getEndLocation() const
{
	if (!m_isStart)
	{
		return this;
	}
	else
	{
		return m_other;
	}
}

inline bool SourceLocation::isStartLocation() const
{
	return m_isStart;
}

inline bool SourceLocation::isEndLocation() const
{
	return !m_isStart;
}

inline bool SourceLocation::isScopeLocation() const
{
	return m_type == LocationType::SCOPE;
}

inline std::ostream& operator<<(std::ostream& ostream, const SourceLocation& location)
{
	if (location.isStartLocation())
	{
		ostream << '<';
	}

	ostream << location.getColumnNumber() << ":[ ";
	for (Id tokenId: location.getTokenIds())
	{
		ostream << '\b' << tokenId << ' ';
	}

	ostream << "\b]";

	if (location.isEndLocation())
	{
		ostream << '>';
	}

	ostream << ' ';
	return ostream;
}
