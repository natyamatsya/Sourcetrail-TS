#include "LogManagerNotifier.h"

#include <string>

#include "MessageStatus.h"
#include "Version.h"
#include "logging.h"

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
