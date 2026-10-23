#ifndef QT_SEARCH_VIEW_H
#define QT_SEARCH_VIEW_H

#include <vector>

#include "SearchView.h"

class QtSearchBar;

class QtSearchView: public SearchView
{
public:
	QtSearchView(ViewLayout* viewLayout);
	~QtSearchView() override;

	// View implementation
	void createWidgetWrapper() override;
	void refreshView() override;

	// SearchView implementation
	std::string getQuery() const override;
	void setMatches(const std::vector<SearchMatch>& matches) override;
	void setFocus() override;
	void findFulltext() override;
	void setAutocompletionList(const std::vector<SearchMatch>& autocompletionList) override;

	// What the search machine shows of an agent's doing (PresentsSearch).
	void presentQuery(const std::string& query);
	void dismissSuggestions();

private:
	void setStyleSheet();


	QtSearchBar* m_widget;
};

#endif	  // QT_SEARCH_VIEW_H
