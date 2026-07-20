// Inline implementations for BookmarkCategory.h. Included at the end of that header; not a
// standalone TU.

#pragma once

inline BookmarkCategory::BookmarkCategory(): m_id(-1) {}

inline BookmarkCategory::BookmarkCategory(const Id id, const std::string& name): m_id(id), m_name(name)
{
}

inline BookmarkCategory::~BookmarkCategory() = default;

inline Id BookmarkCategory::getId() const
{
	return m_id;
}

inline void BookmarkCategory::setId(const Id id)
{
	m_id = id;
}

inline std::string BookmarkCategory::getName() const
{
	return m_name;
}

inline void BookmarkCategory::setName(const std::string& name)
{
	m_name = name;
}
