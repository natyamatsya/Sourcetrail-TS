// Inline implementations for Settings.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "TextAccess.h"
#include "logging.h"
#include "utility.h"
#endif

inline Settings::Settings(const Settings& other)
	: m_config(other.m_config->createCopy()), m_filePath(other.m_filePath)
{
}

inline Settings& Settings::operator=(const Settings& other)
{
	if (&other != this)
	{
		m_filePath = other.m_filePath;
		m_config = other.m_config->createCopy();
	}

	return *this;
}

inline Settings::~Settings() = default;

inline bool Settings::load(const FilePath& filePath, bool readOnly)
{
	m_readOnly = readOnly;

	if (filePath.exists())
	{
		m_config = ConfigManager::createAndLoad(TextAccess::createFromFile(filePath));
		m_filePath = filePath;
		return true;
	}
	else
	{
		clear();
		m_filePath = filePath;
		LOG_WARNING("File for Settings not found: " + filePath.str());
		return false;
	}
}

inline bool Settings::loadFromString(const std::string& text, bool readOnly)
{
	m_readOnly = readOnly;

	m_config = ConfigManager::createAndLoad(TextAccess::createFromString(text));
	m_filePath = FilePath();
	return true;
}

inline bool Settings::save()
{
	if (m_readOnly)
		return false;

	bool success = false;
	if (m_config.get() && !m_filePath.empty())
	{
		if (m_filePath.extension() == ".json")
			success = m_config->saveJson(m_filePath.str());
		else if (m_filePath.extension() == ".toml" || m_filePath.str().ends_with(".srctrl.toml"))
			success = m_config->saveToml(m_filePath.str());
		else
			LOG_ERROR("Unsupported settings format (expected .json or .toml): " + m_filePath.str());
	}

	if (!success)
		LOG_WARNING("Settings were not saved: " + m_filePath.str());

	return success;
}

inline bool Settings::save(const FilePath& filePath)
{
	setFilePath(filePath);

	return save();
}

inline void Settings::clear()
{
	m_config = ConfigManager::createEmpty();
	m_filePath = FilePath();
}

inline const FilePath& Settings::getFilePath() const
{
	return m_filePath;
}

inline size_t Settings::getVersion() const
{
	return getValue<int>("version", 0);
}

inline void Settings::setVersion(size_t version)
{
	setValue<int>("version", static_cast<int>(version));
}

inline Settings::Settings()
{
	clear();
}

inline void Settings::setFilePath(const FilePath& filePath)
{
	m_filePath = filePath;
}

inline std::vector<FilePath> Settings::getPathValues(const std::string& key) const
{
	std::vector<FilePath> paths;
	for (const std::string& value: getValues<std::string>(key, {}))
	{
		paths.push_back(FilePath(value));
	}
	return paths;
}

inline bool Settings::setPathValues(const std::string& key, const std::vector<FilePath>& paths)
{
	std::vector<std::string> values;
	for (const FilePath& path: paths)
	{
		values.push_back(path.str());
	}

	return setValues(key, values);
}

inline bool Settings::isValueDefined(const std::string& key) const
{
	return m_config->isValueDefined(key);
}

inline void Settings::removeValues(const std::string& key)
{
	m_config->removeValues(key);
}

inline void Settings::enableWarnings() const
{
	m_config->setWarnOnEmptyKey(true);
}

inline void Settings::disableWarnings() const
{
	m_config->setWarnOnEmptyKey(false);
}
