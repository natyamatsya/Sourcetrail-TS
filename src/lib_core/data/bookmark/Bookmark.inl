// Inline implementations for Bookmark.h. Included at the end of that header; not a standalone TU.

#pragma once

inline Bookmark::Bookmark(
	const BookmarkId bookmarkId,
	const std::string& name,
	const std::string& comment,
	const TimeStamp& timeStamp,
	const BookmarkCategory& category)
	: m_bookmarkId(bookmarkId)
	, m_name(name)
	, m_comment(comment)
	, m_timeStamp(timeStamp)
	, m_category(category)
{
}

inline bool Bookmark::isValid() const
{
	return m_isValid;
}

inline void Bookmark::setIsValid(const bool isValid)
{
	m_isValid = isValid;
}

inline BookmarkId Bookmark::getId() const
{
	return m_bookmarkId;
}

inline std::string Bookmark::getName() const
{
	return m_name;
}

inline std::string Bookmark::getComment() const
{
	return m_comment;
}

inline TimeStamp Bookmark::getTimeStamp() const
{
	return m_timeStamp;
}

inline BookmarkCategory Bookmark::getCategory() const
{
	return m_category;
}
