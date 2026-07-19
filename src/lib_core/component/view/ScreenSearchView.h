#ifndef SCREEN_SEARCH_VIEW_H
#define SCREEN_SEARCH_VIEW_H

#include "View.h"

class ScreenSearchView: public View
{
public:
	ScreenSearchView(ViewLayout* viewLayout);
	~ScreenSearchView() override;

	// View implementation
	std::string getName() const override;

	virtual void setMatchCount(size_t matchCount) = 0;
	virtual void setMatchIndex(size_t matchIndex) = 0;

	virtual void addResponder(const std::string& name) = 0;
};

#endif	  // SCREEN_SEARCH_VIEW_H
