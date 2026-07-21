#include "LogManagerNotifier.h"

#include <string>

#ifndef SRCTRL_MODULE_BUILD
#include "MessageStatus.h"
#endif
#include "Version.h"
#include "logging.h"

// Imports come AFTER all textual #includes (include-before-import rule: textual libc++
// following BMI-merged declarations trips "cannot add 'abi_tag' in a redeclaration").
#ifdef SRCTRL_MODULE_BUILD
import srctrl.messaging;
#endif

namespace log_manager_detail
{

void notifyLoggingToggled(bool enabled)
{
	if (enabled)
	{
		LOG_INFO(
			std::string("Enabled logging for Sourcetrail version ") +
			Version::getApplicationVersion().toDisplayString());
		MessageStatus("Enabled console and file logging.").dispatch();
	}
	else
	{
		LOG_INFO("Disabled logging");
		MessageStatus("Disabled console and file logging.").dispatch();
	}
}

}	 // namespace log_manager_detail
