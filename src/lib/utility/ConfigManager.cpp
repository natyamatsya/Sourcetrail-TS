#include "ConfigManager.h"

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include <glaze/glaze.hpp>
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
	m_warnedMissingKeys.clear();
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
		warnMissingKey(key);
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
		warnMissingKey(key);
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

	// Detect the format by the first non-whitespace character: '{' is JSON (the
	// ApplicationSettings and color-scheme format), anything else is TOML (used
	// by project .srctrl.toml files). XML support has been removed.
	const size_t firstNonWs = text.find_first_not_of(" \t\r\n");
	const char firstChar = (firstNonWs != std::string::npos) ? text[firstNonWs] : '\0';

	if (firstChar == '<')
	{
		LOG_ERROR("XML configs are no longer supported.");
		return false;
	}
	if (firstChar == '{')
	{
		return loadJson(text);
	}
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

void ConfigManager::setWarnOnEmptyKey(bool warnOnEmptyKey) const
{
	m_warnOnEmptyKey = warnOnEmptyKey;
}

void ConfigManager::warnMissingKey(const std::string& key) const
{
	bool shouldWarn = false;
	{
		std::lock_guard<std::mutex> lock(m_warningMutex);
		if (!m_warnOnEmptyKey)
			return;

		shouldWarn = m_warnedMissingKeys.insert(key).second;
	}

	if (shouldWarn)
		LOG_WARNING("value " + key + " is not present in config.");
}

ConfigManager::ConfigManager() = default;

ConfigManager::ConfigManager(const ConfigManager& other)
	: m_values(other.m_values)
	, m_warnOnEmptyKey(other.m_warnOnEmptyKey)
{
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
				// Plain array: one entry per element under the SAME key — the exact
				// inverse of buildTomlTable, mirroring the JSON scheme. (The previous
				// singular/plural renaming heuristic made repeated-value keys lossy:
				// the writer emitted `source_paths.source_path = [a, b]`, which the
				// reader then mangled to `source_paths/source_path/source_path`.)
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
						m_values.emplace(fullKey, val);
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

namespace
{
// Convert a scalar JSON node to the string representation the flat value store
// uses. Values are written as strings (see buildJsonTree), but be tolerant of a
// hand-edited config that uses native JSON numbers/booleans.
std::string jsonScalarToString(const glz::generic& value)
{
	if (value.is_string())
		return value.get_string();
	if (value.is_boolean())
		return value.get<bool>() ? "1" : "0";
	if (value.is_number())
	{
		const double number = value.get_number();
		double intPart = 0.0;
		if (std::modf(number, &intPart) == 0.0 && std::abs(number) < 1e15)
			return std::to_string(static_cast<long long>(number));
		std::ostringstream oss;
		oss << number;
		return oss.str();
	}
	return "";	  // null
}

// Walk a parsed JSON DOM into the flat "a/b/c" key/value store. Objects nest the
// path; an array leaf emits one entry per element under the same key (the inverse
// of buildJsonTree), so repeated-value lists round-trip without any singular/plural
// renaming — the ambiguity that breaks the TOML serializer.
void parseJsonValue(
	const glz::generic& value,
	const std::string& currentPath,
	std::multimap<std::string, std::string>& values)
{
	if (value.is_object())
	{
		for (const auto& [key, child] : value.get_object())
		{
			const std::string fullKey = currentPath.empty() ? key : currentPath + "/" + key;
			parseJsonValue(child, fullKey, values);
		}
	}
	else if (value.is_array())
	{
		for (const auto& element : value.get_array())
		{
			const std::string str = jsonScalarToString(element);
			if (!str.empty() && !currentPath.empty())
				values.emplace(currentPath, str);
		}
	}
	else if (!currentPath.empty())
	{
		const std::string str = jsonScalarToString(value);
		if (!str.empty())
			values.emplace(currentPath, str);
	}
}

// Build a JSON DOM from the flat value store. A key with a single value becomes a
// scalar leaf; a key with several values (a list) becomes an array leaf. This is
// the exact inverse of parseJsonValue.
glz::generic buildJsonTree(const std::multimap<std::string, std::string>& values)
{
	glz::generic root = glz::generic::object_t{};

	std::set<std::string> processedKeys;
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		const std::string& key = it->first;
		if (!processedKeys.insert(key).second)
			continue;

		std::vector<std::string> vals;
		const auto range = values.equal_range(key);
		for (auto jt = range.first; jt != range.second; ++jt)
			vals.push_back(jt->second);

		const std::vector<std::string> parts = utility::splitToVector(key, "/");
		glz::generic* node = &root;
		for (size_t i = 0; i + 1 < parts.size(); ++i)
			node = &((*node)[parts[i]]);

		glz::generic& leaf = (*node)[parts.back()];
		if (vals.size() == 1)
		{
			leaf = vals.front();
		}
		else
		{
			glz::generic::array_t array;
			for (const std::string& value : vals)
			{
				// Parentheses (not braces) to select the scalar-string constructor;
				// brace-init would hit the initializer_list ctor and wrap each value
				// in a single-element array.
				array.push_back(glz::generic(value));
			}
			leaf = array;
		}
	}

	return root;
}
}	 // namespace

bool ConfigManager::loadJson(const std::string& text)
{
	const auto parsed = glz::read_json<glz::generic>(text);
	if (!parsed)
	{
		LOG_ERROR("Failed to parse JSON config: " + glz::format_error(parsed, text));
		return false;
	}

	parseJsonValue(parsed.value(), "", m_values);
	return true;
}

bool ConfigManager::saveJson(const std::string& filepath)
{
	try
	{
		const auto written = glz::write_json(buildJsonTree(m_values));
		if (!written)
		{
			LOG_ERROR("Failed to serialize JSON config: " + glz::format_error(written.error()));
			return false;
		}

		std::ofstream file(filepath);
		if (!file.is_open())
		{
			LOG_ERROR("Could not open file for writing: " + filepath);
			return false;
		}
		file << glz::prettify_json(written.value()) << '\n';
		return file.good();
	}
	catch (const std::exception& e)
	{
		// buildJsonTree throws (bad_variant_access) when a key is both a leaf and
		// a parent — e.g. a malformed source file with stray text between tags.
		LOG_ERROR(std::string("Failed to save JSON file: ") + e.what());
		return false;
	}
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

std::string ConfigManager::toString() const
{
	// The multimap is ordered, so this rendering is canonical: two stores with
	// the same keys and values (including repeated-value lists) compare equal.
	std::string output;
	for (const auto& [key, value]: m_values)
	{
		output += key + ": " + value + "\n";
	}
	return output;
}
