#ifndef MESSAGE_LOAD_PROJECT_H
#define MESSAGE_LOAD_PROJECT_H

#include "SrctrlModule.h"

// Family-internal includes are unguarded: same module either way.
#include "Message.h"

// Honorary family (payload value types carried by this message; dual-swept, attached here).
#include "RefreshInfo.h"
#include "ShardConfig.h"
#ifndef SRCTRL_MODULE_PURVIEW

#include "FilePath.h"
#include "utilityEnum.h"
#endif

SRCTRL_EXPORT class MessageLoadProject: public Message<MessageLoadProject>
{
public:
	MessageLoadProject(
		const FilePath& filePath,
		bool settingsChanged = false,
		RefreshMode refreshMode = RefreshMode::NONE,
		const ShardConfig& shardConfig = ShardConfig())
		: projectSettingsFilePath(filePath)
		, settingsChanged(settingsChanged)
		, refreshMode(refreshMode)
		, shardConfig(shardConfig)
	{
	}

	static const std::string getStaticType()
	{
		return "MessageLoadProject";
	}

	void print(std::ostream& os) const override
	{
		os << projectSettingsFilePath.str();
		os << ", settingsChanged: " << std::boolalpha << settingsChanged;
		os << ", refreshMode: " << refreshMode;
	}

	const FilePath projectSettingsFilePath;
	const bool settingsChanged;
	const RefreshMode refreshMode;
	const ShardConfig shardConfig;
};

#endif	  // MESSAGE_LOAD_PROJECT_H
