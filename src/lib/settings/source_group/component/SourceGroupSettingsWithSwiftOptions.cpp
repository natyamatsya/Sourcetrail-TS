#include "SourceGroupSettingsWithSwiftOptions.h"

#include "ConfigManager.h"
#include "ProjectSettings.h"
#include "utility.h"
#include "utilityFile.h"

const std::vector<std::string>& SourceGroupSettingsWithSwiftOptions::getSwiftBuildArgs() const
{
	return m_swiftBuildArgs;
}

void SourceGroupSettingsWithSwiftOptions::setSwiftBuildArgs(const std::vector<std::string>& args)
{
	m_swiftBuildArgs = args;
}

const FilePath& SourceGroupSettingsWithSwiftOptions::getSwiftToolchainPath() const
{
	return m_swiftToolchainPath;
}

FilePath SourceGroupSettingsWithSwiftOptions::getSwiftToolchainPathExpandedAndAbsolute() const
{
	if (m_swiftToolchainPath.empty())
		return FilePath();
	return utility::getExpandedAndAbsolutePath(
		m_swiftToolchainPath, getProjectSettings()->getProjectDirectoryPath());
}

void SourceGroupSettingsWithSwiftOptions::setSwiftToolchainPath(const FilePath& path)
{
	m_swiftToolchainPath = path;
}

const FilePath& SourceGroupSettingsWithSwiftOptions::getSwiftIndexStorePath() const
{
	return m_swiftIndexStorePath;
}

FilePath SourceGroupSettingsWithSwiftOptions::getSwiftIndexStorePathExpandedAndAbsolute() const
{
	if (m_swiftIndexStorePath.empty())
		return FilePath();
	return utility::getExpandedAndAbsolutePath(
		m_swiftIndexStorePath, getProjectSettings()->getProjectDirectoryPath());
}

void SourceGroupSettingsWithSwiftOptions::setSwiftIndexStorePath(const FilePath& path)
{
	m_swiftIndexStorePath = path;
}

const std::string& SourceGroupSettingsWithSwiftOptions::getSwiftSpecializationScope() const
{
	return m_swiftSpecializationScope;
}

void SourceGroupSettingsWithSwiftOptions::setSwiftSpecializationScope(const std::string& scope)
{
	m_swiftSpecializationScope = scope;
}

bool SourceGroupSettingsWithSwiftOptions::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithSwiftOptions* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithSwiftOptions*>(other);

	return (
		otherPtr && m_swiftBuildArgs == otherPtr->m_swiftBuildArgs &&
		m_swiftToolchainPath == otherPtr->m_swiftToolchainPath &&
		m_swiftIndexStorePath == otherPtr->m_swiftIndexStorePath &&
		m_swiftSpecializationScope == otherPtr->m_swiftSpecializationScope);
}

void SourceGroupSettingsWithSwiftOptions::load(const ConfigManager* config, const std::string& key)
{
	setSwiftBuildArgs(config->getValuesOrDefaults(
		key + "/swift_build_args/swift_build_arg", std::vector<std::string>()));
	setSwiftToolchainPath(
		FilePath(config->getValueOrDefault(key + "/swift_toolchain_path", std::string{})));
	setSwiftIndexStorePath(
		FilePath(config->getValueOrDefault(key + "/swift_index_store_path", std::string{})));
	setSwiftSpecializationScope(
		config->getValueOrDefault(key + "/swift_specialization_scope", std::string{"local"}));
}

void SourceGroupSettingsWithSwiftOptions::save(ConfigManager* config, const std::string& key)
{
	config->setValues(key + "/swift_build_args/swift_build_arg", getSwiftBuildArgs());
	config->setValue(key + "/swift_toolchain_path", getSwiftToolchainPath().str());
	config->setValue(key + "/swift_index_store_path", getSwiftIndexStorePath().str());
	config->setValue(key + "/swift_specialization_scope", getSwiftSpecializationScope());
}
