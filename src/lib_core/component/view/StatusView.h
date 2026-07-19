#ifndef STATUS_VIEW_H
#define STATUS_VIEW_H

#include <vector>

#include "Status.h"
#include "View.h"

class StatusView: public View
{
public:
	StatusView(ViewLayout* viewLayout);
	~StatusView() override;

	std::string getName() const override;

	virtual void addStatus(const std::vector<Status>& status) = 0;
	virtual void clear() = 0;
};

#endif	  // STATUS_VIEW_H
