#ifndef BOOKMARK_CATEGORY_H
#define BOOKMARK_CATEGORY_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <string>

#include "types.h"
#endif

SRCTRL_EXPORT class BookmarkCategory
{
public:
	BookmarkCategory();
	BookmarkCategory(const Id id, const std::string& name);
	~BookmarkCategory();

	Id getId() const;
	void setId(const Id id);

	std::string getName() const;
	void setName(const std::string& name);

private:
	Id m_id;
	std::string m_name;
};

#include "BookmarkCategory.inl"

#endif	  // BOOKMARK_CATEGORY_H
