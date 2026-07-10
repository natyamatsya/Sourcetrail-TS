#include "SourceGroupSettingsWithCargoOptions.h"

#include "ConfigManager.h"
#include "utility.h"

const std::vector<std::string>& SourceGroupSettingsWithCargoOptions::getCargoFeatures() const
{
	return m_cargoFeatures;
}

void SourceGroupSettingsWithCargoOptions::setCargoFeatures(const std::vector<std::string>& features)
{
	m_cargoFeatures = features;
}

bool SourceGroupSettingsWithCargoOptions::getCargoAllFeatures() const
{
	return m_cargoAllFeatures;
}

void SourceGroupSettingsWithCargoOptions::setCargoAllFeatures(bool allFeatures)
{
	m_cargoAllFeatures = allFeatures;
}

bool SourceGroupSettingsWithCargoOptions::getCargoNoDefaultFeatures() const
{
	return m_cargoNoDefaultFeatures;
}

void SourceGroupSettingsWithCargoOptions::setCargoNoDefaultFeatures(bool noDefaultFeatures)
{
	m_cargoNoDefaultFeatures = noDefaultFeatures;
}

const std::string& SourceGroupSettingsWithCargoOptions::getCargoTargetTriple() const
{
	return m_cargoTargetTriple;
}

void SourceGroupSettingsWithCargoOptions::setCargoTargetTriple(const std::string& targetTriple)
{
	m_cargoTargetTriple = targetTriple;
}

const std::string& SourceGroupSettingsWithCargoOptions::getRustSpecializationScope() const
{
	return m_rustSpecializationScope;
}

void SourceGroupSettingsWithCargoOptions::setRustSpecializationScope(const std::string& scope)
{
	m_rustSpecializationScope = scope;
}

bool SourceGroupSettingsWithCargoOptions::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithCargoOptions* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithCargoOptions*>(other);

	return (
		otherPtr && utility::isPermutation(m_cargoFeatures, otherPtr->m_cargoFeatures) &&
		m_cargoAllFeatures == otherPtr->m_cargoAllFeatures &&
		m_cargoNoDefaultFeatures == otherPtr->m_cargoNoDefaultFeatures &&
		m_cargoTargetTriple == otherPtr->m_cargoTargetTriple &&
		m_rustSpecializationScope == otherPtr->m_rustSpecializationScope);
}

void SourceGroupSettingsWithCargoOptions::load(const ConfigManager* config, const std::string& key)
{
	setCargoFeatures(config->getValuesOrDefaults(
		key + "/cargo_features/cargo_feature", std::vector<std::string>()));
	setCargoAllFeatures(config->getValueOrDefault(key + "/cargo_all_features", false));
	setCargoNoDefaultFeatures(config->getValueOrDefault(key + "/cargo_no_default_features", false));
	setCargoTargetTriple(config->getValueOrDefault<std::string>(key + "/cargo_target_triple", ""));
	setRustSpecializationScope(
		config->getValueOrDefault<std::string>(key + "/rust_specialization_scope", "local"));
}

void SourceGroupSettingsWithCargoOptions::save(ConfigManager* config, const std::string& key)
{
	config->setValues(key + "/cargo_features/cargo_feature", getCargoFeatures());
	config->setValue(key + "/cargo_all_features", getCargoAllFeatures());
	config->setValue(key + "/cargo_no_default_features", getCargoNoDefaultFeatures());
	config->setValue(key + "/cargo_target_triple", getCargoTargetTriple());
	config->setValue(key + "/rust_specialization_scope", getRustSpecializationScope());
}
