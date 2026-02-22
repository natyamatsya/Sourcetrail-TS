#include "ConfigManager.h"

#include <fstream>
#include <set>
#include <sstream>

#include "tinyxml.h"
#include <toml++/toml.hpp>

#include "FilePath.h"
#include "TextAccess.h"
#include "logging.h"
#include "utility.h"
#include "utilityString.h"

std::shared_ptr<ConfigManager> ConfigManager::createEmpty()
{
	return std::shared_ptr<ConfigManager>(new ConfigManager());
}

std::shared_ptr<ConfigManager> ConfigManager::createAndLoad(const std::shared_ptr<TextAccess> textAccess)
{
	std::shared_ptr<ConfigManager> configManager = std::shared_ptr<ConfigManager>(new ConfigManager());
	configManager->load(textAccess);
	return configManager;
}

std::shared_ptr<ConfigManager> ConfigManager::createCopy()
{
	return std::shared_ptr<ConfigManager>(new ConfigManager(*this));
}

void ConfigManager::clear()
{
	m_values.clear();
}

bool ConfigManager::getValue(const std::string& key, std::string& value) const
{
	std::multimap<std::string, std::string>::const_iterator it = m_values.find(key);

	if (it != m_values.end())
	{
		value = it->second;
		return true;
	}
	else
	{
		if (m_warnOnEmptyKey)
		{
			LOG_WARNING("value " + key + " is not present in config.");
		}
		return false;
	}
}

bool ConfigManager::getValue(const std::string& key, int& value) const
{
	std::string valueString;
	if (getValue(key, valueString))
	{
		value = atoi(valueString.c_str());
		return true;
	}
	return false;
}

bool ConfigManager::getValue(const std::string& key, float& value) const
{
	std::string valueString;
	if (getValue(key, valueString))
	{
		std::stringstream ss;
		ss << valueString;
		ss >> value;
		return true;
	}
	return false;
}

bool ConfigManager::getValue(const std::string& key, bool& value) const
{
	std::string valueString;
	if (getValue(key, valueString))
	{
		value = (atoi(valueString.c_str()) != 0);
		return true;
	}
	return false;
}

bool ConfigManager::getValue(const std::string& key, FilePath& value) const
{
	std::string valueString;
	if (getValue(key, valueString))
	{
		value = FilePath(valueString);
		return true;
	}
	return false;
}

bool ConfigManager::getValues(const std::string& key, std::vector<std::string>& values) const
{
	std::pair<
		std::multimap<std::string, std::string>::const_iterator,
		std::multimap<std::string, std::string>::const_iterator>
		ret;
	ret = m_values.equal_range(key);

	if (ret.first != ret.second)
	{
		for (std::multimap<std::string, std::string>::const_iterator cit = ret.first;
			 cit != ret.second;
			 ++cit)
		{
			values.push_back(cit->second);
		}
		return true;
	}
	else
	{
		if (m_warnOnEmptyKey)
		{
			LOG_WARNING("value " + key + " is not present in config.");
		}
		return false;
	}
}

bool ConfigManager::getValues(const std::string& key, std::vector<int>& values) const
{
	std::vector<std::string> valuesStringVector;
	if (getValues(key, valuesStringVector))
	{
		for (const std::string& valueString: valuesStringVector)
		{
			values.push_back(atoi(valueString.c_str()));
		}
		return true;
	}
	return false;
}

bool ConfigManager::getValues(const std::string& key, std::vector<float>& values) const
{
	std::vector<std::string> valuesStringVector;
	if (getValues(key, valuesStringVector))
	{
		for (const std::string& valueString: valuesStringVector)
		{
			values.push_back(static_cast<float>(atof(valueString.c_str())));
		}
		return true;
	}
	return false;
}

bool ConfigManager::getValues(const std::string& key, std::vector<bool>& values) const
{
	std::vector<std::string> valuesStringVector;
	if (getValues(key, valuesStringVector))
	{
		for (const std::string& valueString: valuesStringVector)
		{
			values.push_back(atoi(valueString.c_str()) != 0);
		}
		return true;
	}
	return false;
}

bool ConfigManager::getValues(const std::string& key, std::vector<FilePath>& values) const
{
	std::vector<std::string> valuesStringVector;
	if (getValues(key, valuesStringVector))
	{
		for (const std::string& valueString: valuesStringVector)
		{
			values.push_back(FilePath(valueString));
		}
		return true;
	}
	return false;
}

void ConfigManager::setValue(const std::string& key, const std::string& value)
{
	std::multimap<std::string, std::string>::iterator it = m_values.find(key);

	if (it != m_values.end())
	{
		it->second = value;
	}
	else
	{
		m_values.emplace(key, value);
	}
}

void ConfigManager::setValue(const std::string& key, const int value)
{
	setValue(key, std::to_string(value));
}

void ConfigManager::setValue(const std::string& key, const float value)
{
	std::stringstream ss;
	ss << value;
	setValue(key, ss.str());
}

void ConfigManager::setValue(const std::string& key, const bool value)
{
	setValue(key, std::string(value ? "1" : "0"));
}

void ConfigManager::setValue(const std::string& key, const FilePath& value)
{
	setValue(key, value.str());
}

void ConfigManager::setValues(const std::string& key, const std::vector<std::string>& values)
{
	std::multimap<std::string, std::string>::iterator it = m_values.find(key);

	if (it != m_values.end())
	{
		m_values.erase(key);
	}
	for (std::string s: values)
	{
		m_values.emplace(key, s);
	}
}

void ConfigManager::setValues(const std::string& key, const std::vector<int>& values)
{
	std::vector<std::string> stringValues;
	for (int i: values)
	{
		stringValues.push_back(std::to_string(i));
	}
	setValues(key, stringValues);
}

void ConfigManager::setValues(const std::string& key, const std::vector<float>& values)
{
	std::vector<std::string> stringValues;
	for (float f: values)
	{
		stringValues.push_back(std::to_string(f));
	}
	setValues(key, stringValues);
}

void ConfigManager::setValues(const std::string& key, const std::vector<bool>& values)
{
	std::vector<std::string> stringValues;
	for (bool b: values)
	{
		stringValues.push_back(std::string(b ? "1" : "0"));
	}
	setValues(key, stringValues);
}

void ConfigManager::setValues(const std::string& key, const std::vector<FilePath>& values)
{
	std::vector<std::string> stringValues;
	for (const FilePath& p: values)
	{
		stringValues.push_back(p.str());
	}
	setValues(key, stringValues);
}

void ConfigManager::removeValues(const std::string& key)
{
	for (const std::string& sublevelKey: getSublevelKeys(key))
	{
		removeValues(sublevelKey);
	}
	m_values.erase(key);
}

bool ConfigManager::isValueDefined(const std::string& key) const
{
	std::multimap<std::string, std::string>::const_iterator it = m_values.find(key);

	return (it != m_values.end());
}

std::vector<std::string> ConfigManager::getSublevelKeys(const std::string& key) const
{
	std::set<std::string> keys;
	for (std::multimap<std::string, std::string>::const_iterator it = m_values.begin();
		 it != m_values.end();
		 it++)
	{
		if (utility::isPrefix(key, it->first))
		{
			size_t startPos = it->first.find("/", key.size());
			if (startPos == key.size())
			{
				std::string sublevelKey = it->first.substr(0, it->first.find("/", startPos + 1));
				keys.insert(sublevelKey);
			}
		}
	}
	return utility::toVector(keys);
}

bool ConfigManager::load(const std::shared_ptr<TextAccess> textAccess)
{
	const std::string& text = textAccess->getText();

	if (text.find("<?xml") != std::string::npos)
	{
		TiXmlDocument doc;
		const char* pTest = doc.Parse(text.c_str(), nullptr, TIXML_ENCODING_UTF8);
		if (pTest != nullptr)
		{
			TiXmlHandle docHandle(&doc);
			TiXmlNode* rootNode = docHandle.FirstChild("config").ToNode();
			if (rootNode == nullptr)
			{
				LOG_ERROR("No rootelement 'config' in the configfile");
				return false;
			}
			for (TiXmlNode* childNode = rootNode->FirstChild(); childNode;
				 childNode = childNode->NextSibling())
			{
				parseSubtree(childNode, "");
			}
		}
		else
		{
			LOG_ERROR("Unable to load XML file.");
			return false;
		}
	}
	else
	{
		try
		{
			std::istringstream stream(text);
			toml::table root = toml::parse(stream);
			parseTomlTable(root, "");
		}
		catch (const toml::parse_error& e)
		{
			LOG_ERROR(std::string("Unable to parse TOML file: ") + e.what());
			return false;
		}
	}
	return true;
}

bool ConfigManager::save(const std::string filepath)
{
	std::string output;
	return createXmlDocument(true, filepath, output);
}

void ConfigManager::setWarnOnEmptyKey(bool warnOnEmptyKey) const
{
	m_warnOnEmptyKey = warnOnEmptyKey;
}

ConfigManager::ConfigManager() = default;

ConfigManager::ConfigManager(const ConfigManager& other) = default;

bool ConfigManager::createXmlDocument(bool saveAsFile, const std::string filepath, std::string& output)
{
	bool success = true;
	TiXmlDocument doc;
	TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "utf-8", "");
	doc.LinkEndChild(decl);
	TiXmlElement* root = new TiXmlElement("config");
	doc.LinkEndChild(root);

	for (std::multimap<std::string, std::string>::iterator it = m_values.begin();
		 it != m_values.end();
		 ++it)
	{
		if (!it->first.size() || !it->second.size())
		{
			continue;
		}

		std::vector<std::string> tokens = utility::splitToVector(it->first, "/");

		TiXmlElement* element = doc.RootElement();
		TiXmlElement* child;
		while (tokens.size() > 1)
		{
			child = element->FirstChildElement(tokens.front().c_str());
			if (!child)
			{
				child = new TiXmlElement(tokens.front().c_str());
				element->LinkEndChild(child);
			}
			tokens.erase(tokens.begin());
			element = child;
		}

		child = new TiXmlElement(tokens.front().c_str());
		element->LinkEndChild(child);
		TiXmlText* text = new TiXmlText(it->second.c_str());
		child->LinkEndChild(text);
	}

	if (saveAsFile)
	{
		success = doc.SaveFile(filepath.c_str());
	}
	else
	{
		TiXmlPrinter printer;
		doc.Accept(&printer);
		output = printer.CStr();
	}
	success = doc.SaveFile(filepath.c_str());
	doc.Clear();
	return success;
}

void ConfigManager::parseSubtree(TiXmlNode* currentNode, const std::string& currentPath)
{
	if (currentNode->Type() == TiXmlNode::TINYXML_TEXT)
	{
		std::string key = currentPath.substr(0, currentPath.size() - 1);
		m_values.insert(std::pair<std::string, std::string>(key, currentNode->ToText()->Value()));
	}
	else
	{
		for (TiXmlNode* childNode = currentNode->FirstChild(); childNode;
			 childNode = childNode->NextSibling())
		{
			parseSubtree(childNode, currentPath + std::string(currentNode->Value()) + "/");
		}
	}
}

void ConfigManager::parseTomlTable(const toml::v3::table& table, const std::string& currentPath)
{
	for (auto&& [k, v] : table)
	{
		const std::string key(k.str());
		const std::string fullKey = currentPath.empty() ? key : currentPath + "/" + key;

		if (const auto* arr = v.as_array())
		{
			if (arr->is_array_of_tables())
			{
				// Array of tables: each element must have an "id" field so we can
				// reconstruct the XML-style "source_group_<UUID>" key prefix.
				for (auto&& elem : *arr)
				{
					const auto* elemTable = elem.as_table();
					if (!elemTable)
						continue;

					const auto* idNode = elemTable->get("id");
					if (idNode && idNode->is_string())
					{
						const std::string id = idNode->as_string()->get();
						const std::string groupKey = fullKey + "/source_group_" + id;
						parseTomlTable(*elemTable, groupKey);
					}
					else
					{
						parseTomlTable(*elemTable, fullKey);
					}
				}
			}
			else
			{
				// Plain array: emit one entry per element using the singular key name.
				// E.g. source_paths = ["a", "b"] -> source_paths/source_path = "a", "b"
				const std::string singularKey = (key.size() > 1 && key.back() == 's')
					? fullKey + "/" + key.substr(0, key.size() - 1)
					: fullKey + "/" + key;

				for (auto&& elem : *arr)
				{
					std::string val;
					if (elem.is_string())
						val = elem.as_string()->get();
					else if (elem.is_integer())
						val = std::to_string(elem.as_integer()->get());
					else if (elem.is_boolean())
						val = elem.as_boolean()->get() ? "1" : "0";
					else if (elem.is_floating_point())
					{
						std::ostringstream oss;
						oss << elem.as_floating_point()->get();
						val = oss.str();
					}
					if (!val.empty())
						m_values.emplace(singularKey, val);
				}
			}
		}
		else if (const auto* subTable = v.as_table())
		{
			parseTomlTable(*subTable, fullKey);
		}
		else
		{
			// Skip the "id" field itself — it was already consumed to build the key prefix.
			if (key == "id" && currentPath.find("source_group_") != std::string::npos)
				continue;

			std::string val;
			if (v.is_string())
				val = v.as_string()->get();
			else if (v.is_integer())
				val = std::to_string(v.as_integer()->get());
			else if (v.is_boolean())
				val = v.as_boolean()->get() ? "1" : "0";
			else if (v.is_floating_point())
			{
				std::ostringstream oss;
				oss << v.as_floating_point()->get();
				val = oss.str();
			}

			if (!val.empty())
				m_values.emplace(fullKey, val);
		}
	}
}

toml::v3::table ConfigManager::buildTomlTable() const
{
	toml::table root;

	// Collect source_group UUIDs in insertion order (preserve order from multimap).
	std::vector<std::string> sgOrder;
	{
		std::set<std::string> seen;
		for (auto& [k, v] : m_values)
		{
			const std::string prefix = "source_groups/source_group_";
			if (k.rfind(prefix, 0) == 0)
			{
				const std::string rest = k.substr(prefix.size());
				const std::string uuid = rest.substr(0, rest.find('/'));
				if (seen.insert(uuid).second)
					sgOrder.push_back(uuid);
			}
		}
	}

	// Helper: insert a string value into a nested toml::table by a '/'-separated path.
	// Returns false if the path collides with an existing non-table node.
	std::function<void(toml::table&, const std::vector<std::string>&, size_t, const std::string&)>
		insertValue = [&](toml::table& tbl, const std::vector<std::string>& parts,
						  size_t idx, const std::string& val)
	{
		if (idx == parts.size() - 1)
		{
			const std::string& leaf = parts[idx];
			if (auto* existing = tbl.get(leaf))
			{
				if (auto* arr = existing->as_array())
					arr->push_back(val);
				else
				{
					toml::array a;
					a.push_back(existing->as_string()->get());
					a.push_back(val);
					tbl.insert_or_assign(leaf, std::move(a));
				}
			}
			else
			{
				tbl.insert_or_assign(leaf, val);
			}
			return;
		}

		const std::string& seg = parts[idx];
		if (!tbl.contains(seg))
			tbl.insert_or_assign(seg, toml::table{});
		insertValue(*tbl.get(seg)->as_table(), parts, idx + 1, val);
	};

	// Build [[source_groups]] array of tables.
	toml::array sgArray;
	for (const std::string& uuid : sgOrder)
	{
		const std::string sgPrefix = "source_groups/source_group_" + uuid + "/";
		toml::table entry;
		entry.insert_or_assign("id", uuid);

		// Collect all keys belonging to this source group.
		for (auto it = m_values.begin(); it != m_values.end(); )
		{
			if (it->first.rfind(sgPrefix, 0) != 0)
			{
				++it;
				continue;
			}

			const std::string relKey = it->first.substr(sgPrefix.size());
			std::vector<std::string> parts = utility::splitToVector(relKey, "/");

			// Gather all values for this exact key (multimap may have duplicates).
			std::vector<std::string> vals;
			auto range = m_values.equal_range(it->first);
			for (auto jt = range.first; jt != range.second; ++jt)
				vals.push_back(jt->second);

			if (vals.size() == 1)
			{
				insertValue(entry, parts, 0, vals[0]);
			}
			else
			{
				// Multiple values → array at the leaf.
				// Navigate to the parent table.
				toml::table* cur = &entry;
				for (size_t i = 0; i + 1 < parts.size(); ++i)
				{
					if (!cur->contains(parts[i]))
						cur->insert_or_assign(parts[i], toml::table{});
					cur = cur->get(parts[i])->as_table();
				}
				toml::array arr;
				for (const std::string& v : vals)
					arr.push_back(v);
				cur->insert_or_assign(parts.back(), std::move(arr));
			}

			it = range.second;
		}

		sgArray.push_back(std::move(entry));
	}

	if (!sgArray.empty())
		root.insert_or_assign("source_groups", std::move(sgArray));

	// All non-source_group keys.
	std::set<std::string> processedKeys;
	for (auto it = m_values.begin(); it != m_values.end(); )
	{
		const std::string& key = it->first;

		if (key.rfind("source_groups/source_group_", 0) == 0)
		{
			++it;
			continue;
		}

		if (processedKeys.count(key))
		{
			++it;
			continue;
		}
		processedKeys.insert(key);

		std::vector<std::string> parts = utility::splitToVector(key, "/");

		std::vector<std::string> vals;
		auto range = m_values.equal_range(key);
		for (auto jt = range.first; jt != range.second; ++jt)
			vals.push_back(jt->second);

		if (vals.size() == 1)
		{
			insertValue(root, parts, 0, vals[0]);
		}
		else
		{
			toml::table* cur = &root;
			for (size_t i = 0; i + 1 < parts.size(); ++i)
			{
				if (!cur->contains(parts[i]))
					cur->insert_or_assign(parts[i], toml::table{});
				cur = cur->get(parts[i])->as_table();
			}
			toml::array arr;
			for (const std::string& v : vals)
				arr.push_back(v);
			cur->insert_or_assign(parts.back(), std::move(arr));
		}

		it = range.second;
	}

	return root;
}

bool ConfigManager::saveToml(const std::string& filepath)
{
	try
	{
		toml::table root = buildTomlTable();
		std::ofstream file(filepath);
		if (!file.is_open())
		{
			LOG_ERROR("Could not open file for writing: " + filepath);
			return false;
		}
		file << root << '\n';
		return file.good();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(std::string("Failed to save TOML file: ") + e.what());
		return false;
	}
}

std::string ConfigManager::toString()
{
	std::string output;
	createXmlDocument(false, "", output);
	return output;
}
