#ifndef ERROR_VIEW_H
#define ERROR_VIEW_H

#include <vector>

#include "ErrorCountInfo.h"
#include "ErrorFilter.h"
#include "ErrorInfo.h"
#include "View.h"

class ErrorView: public View
{
public:
	ErrorView(ViewLayout* viewLayout);
	~ErrorView() override;

	std::string getName() const override;

	virtual void clear() = 0;

	virtual void addErrors(
		const std::vector<ErrorInfo>& errors, const ErrorCountInfo& errorCount, bool scrollTo) = 0;
	virtual void setErrorId(Id errorId) = 0;

	virtual ErrorFilter getErrorFilter() const = 0;
	virtual void setErrorFilter(const ErrorFilter& filter) = 0;

	//! Pop up the first-run error-help message. The presentation (widget, UI thread) is the view's
	//! concern, so the controller drives it through this seam instead of touching Qt directly.
	virtual void showErrorHelpMessage() = 0;
};

#endif	  // ERROR_VIEW_H
