// The concrete-type factory, isolated from every header cycle: included ONLY by
//   - ProjectSettingsFactory.cpp (classic build: forced odr-use emits the ordinary weak def), and
//   - the srctrl.settings wrapper, LAST (module build: importers self-instantiate the inline
//     definition from the BMI, so their references stay ordinary-mangled and merge).

#pragma once

// Family-internal, unguarded.
#include "ProjectSettings.h"
#include "SourceGroupSettingsCustomCommand.h"
#include "SourceGroupSettingsUnloadable.h"
#include "SourceGroupSettingsCEmpty.h"
#include "SourceGroupSettingsCppEmpty.h"
#include "SourceGroupSettingsCxxCdb.h"
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#include "SourceGroupSettingsRustEmpty.h"
#include "SourceGroupSettingsSwiftEmpty.h"
#include "SourceGroupSettingsZigEmpty.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "language_package_flags.h"
#include "logging.h"
#endif

namespace project_settings_detail
{
template <bool Enabled, typename T>
inline std::shared_ptr<SourceGroupSettings> makeIfEnabled(const std::string& id, const ProjectSettings* owner)
{
	if constexpr (Enabled)
		return std::make_shared<T>(id, owner);
	else
		return std::make_shared<SourceGroupSettingsUnloadable>(id, owner);
}
}	 // namespace project_settings_detail

inline std::vector<std::shared_ptr<SourceGroupSettings>> ProjectSettings::getAllSourceGroupSettings() const
{
	std::vector<std::shared_ptr<SourceGroupSettings>> allSettings;
	for (const std::string& key: m_config->getSublevelKeys("source_groups"))
	{
		const std::string id = key.substr(std::string(SourceGroupSettings::s_keyPrefix).length());
		const SourceGroupType type = stringToSourceGroupType(
			getValue<std::string>(key + "/type", ""));

		std::shared_ptr<SourceGroupSettings> settings;

		switch (type)
		{
		case SourceGroupType::C_EMPTY:
			settings = project_settings_detail::makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCEmpty>(id, this);
			break;
		case SourceGroupType::CXX_EMPTY:
			settings = project_settings_detail::makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCppEmpty>(id, this);
			break;
		case SourceGroupType::CXX_CDB:
			settings = project_settings_detail::makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCxxCdb>(id, this);
			break;
		case SourceGroupType::CXX_CMAKE_FILE_API:
			settings = project_settings_detail::makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCxxCMakeFileAPI>(id, this);
			break;
		case SourceGroupType::RUST_EMPTY:
			settings = project_settings_detail::makeIfEnabled<language_packages::buildRustLanguagePackage, SourceGroupSettingsRustEmpty>(id, this);
			break;
		case SourceGroupType::SWIFT_EMPTY:
			settings = project_settings_detail::makeIfEnabled<language_packages::buildSwiftLanguagePackage, SourceGroupSettingsSwiftEmpty>(id, this);
			break;
		case SourceGroupType::ZIG_EMPTY:
			settings = project_settings_detail::makeIfEnabled<language_packages::buildZigLanguagePackage, SourceGroupSettingsZigEmpty>(id, this);
			break;
		case SourceGroupType::CUSTOM_COMMAND:
			settings = std::make_shared<SourceGroupSettingsCustomCommand>(id, this);
			break;
		default:
			settings = std::make_shared<SourceGroupSettingsUnloadable>(id, this);
		}

		if (settings)
		{
			settings->loadSettings(m_config.get());
			allSettings.push_back(settings);
		}
		else
		{
			LOG_WARNING("Sourcegroup with id \"" + id + "\" could not be loaded.");
		}
	}

	return allSettings;
}
