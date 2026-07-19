#ifndef REFRESH_VIEW_H
#define REFRESH_VIEW_H

#include "View.h"

class RefreshController;

class RefreshView: public View
{
public:
	RefreshView(ViewLayout* viewLayout);
	~RefreshView() override;

	std::string getName() const override;

private:
	RefreshController* getController();
};

#endif	  // REFRESH_VIEW_H
