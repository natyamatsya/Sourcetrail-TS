// The one ProjectSettings member that names the concrete source-group settings types: the factory.
// It stays a CLASSIC out-of-line member (not in ProjectSettings.inl) for two reasons:
//   1. Members of module-attached classes keep ordinary mangling (NameHierarchy::deserialize
//      precedent), so this definition links for classic TUs and importers alike.
//   2. Keeping the type/*.h includes out of the inl chain breaks the include cycle
//      WithComponents.h -> SourceGroupSettings.inl -> ProjectSettings.h -> type/*.h ->
//      WithComponents.h.
#include "ProjectSettings.h"

#include "language_package_flags.h"
#include "logging.h"

#include "SourceGroupSettingsCustomCommand.h"
#include "SourceGroupSettingsUnloadable.h"

#include "SourceGroupSettingsCEmpty.h"
#include "SourceGroupSettingsCppEmpty.h"
#include "SourceGroupSettingsCxxCdb.h"
#include "SourceGroupSettingsCxxCMakeFileAPI.h"
#include "SourceGroupSettingsRustEmpty.h"
#include "SourceGroupSettingsSwiftEmpty.h"
#include "SourceGroupSettingsZigEmpty.h"

namespace
{
template <bool Enabled, typename T>
std::shared_ptr<SourceGroupSettings> makeIfEnabled(const std::string& id, const ProjectSettings* owner)
{
	if constexpr (Enabled)
		return std::make_shared<T>(id, owner);
	else
		return std::make_shared<SourceGroupSettingsUnloadable>(id, owner);
}
}	 // namespace

std::vector<std::shared_ptr<SourceGroupSettings>> ProjectSettings::getAllSourceGroupSettings() const
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
			settings = makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCEmpty>(id, this);
			break;
		case SourceGroupType::CXX_EMPTY:
			settings = makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCppEmpty>(id, this);
			break;
		case SourceGroupType::CXX_CDB:
			settings = makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCxxCdb>(id, this);
			break;
		case SourceGroupType::CXX_CMAKE_FILE_API:
			settings = makeIfEnabled<language_packages::buildCxxLanguagePackage, SourceGroupSettingsCxxCMakeFileAPI>(id, this);
			break;
		case SourceGroupType::RUST_EMPTY:
			settings = makeIfEnabled<language_packages::buildRustLanguagePackage, SourceGroupSettingsRustEmpty>(id, this);
			break;
		case SourceGroupType::SWIFT_EMPTY:
			settings = makeIfEnabled<language_packages::buildSwiftLanguagePackage, SourceGroupSettingsSwiftEmpty>(id, this);
			break;
		case SourceGroupType::ZIG_EMPTY:
			settings = makeIfEnabled<language_packages::buildZigLanguagePackage, SourceGroupSettingsZigEmpty>(id, this);
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
