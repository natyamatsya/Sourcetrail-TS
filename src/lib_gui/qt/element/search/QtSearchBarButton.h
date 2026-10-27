#ifndef QT_SEARCH_BAR_BUTTON_H
#define QT_SEARCH_BAR_BUTTON_H

#include "QtSelfRefreshIconButton.h"

class QtSearchBarButton: public QtSelfRefreshIconButton
{
public:
	enum class Size
	{
		SMALL,	  // the code view's navigation buttons
		TOOLBAR,  // the search bar's: a flat 28 px square with a 16 px line icon
		FIELD	  // inside the search field: a flat 24 px square with a 16 px line icon
	};

	QtSearchBarButton(const FilePath& iconPath, Size size = Size::TOOLBAR);

protected:
	void refresh() override;

private:
	Size m_size;
};

#endif	  // QT_SEARCH_BAR_BUTTON_H
