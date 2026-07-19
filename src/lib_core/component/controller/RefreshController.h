#ifndef REFRESH_CONTROLLER_H
#define REFRESH_CONTROLLER_H

#include "Controller.h"

class RefreshView;

class RefreshController: public Controller
{
public:
	RefreshController();
	~RefreshController() override;

	void clear() override;

private:
	RefreshView* getView();
};

#endif	  // REFRESH_CONTROLLER_H
