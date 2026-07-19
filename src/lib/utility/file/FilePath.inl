// Inline implementations for FilePath.h. Included at the end of that header; not a standalone TU.

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include "logging.h"
#include "utilityString.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>
#endif

inline FilePath::FilePath()
	: m_path()
	, m_exists(false)
	, m_checkedExists(false)
	, m_isDirectory(false)
	, m_checkedIsDirectory(false)
	, m_canonicalized(false)
{
}

inline FilePath::FilePath(const char filePath[])
    : FilePath(std::string(filePath))
{
}

inline FilePath::FilePath(const std::string& filePath)
	: m_path(filePath)
	, m_exists(false)
	, m_checkedExists(false)
	, m_isDirectory(false)
	, m_checkedIsDirectory(false)
	, m_canonicalized(false)
{
}

inline FilePath::FilePath(const FilePath& other)
	: m_path(other.getPath())
	, m_exists(other.m_exists)
	, m_checkedExists(other.m_checkedExists)
	, m_isDirectory(other.m_isDirectory)
	, m_checkedIsDirectory(other.m_checkedIsDirectory)
	, m_canonicalized(other.m_canonicalized)
{
}

inline FilePath::FilePath(FilePath&& other)
	: m_path(std::move(other.m_path))
	, m_exists(other.m_exists)
	, m_checkedExists(other.m_checkedExists)
	, m_isDirectory(other.m_isDirectory)
	, m_checkedIsDirectory(other.m_checkedIsDirectory)
	, m_canonicalized(other.m_canonicalized)
{
}

inline FilePath::FilePath(const std::string& filePath, const std::string& base)
	: m_path(std::filesystem::absolute(std::filesystem::path(base) / filePath))
	, m_exists(false)
	, m_checkedExists(false)
	, m_isDirectory(false)
	, m_checkedIsDirectory(false)
	, m_canonicalized(false)
{
}

inline FilePath::~FilePath() = default;

inline const std::filesystem::path &FilePath::getPath() const
{
	return m_path;
}

inline bool FilePath::empty() const
{
	return m_path.empty();
}

inline bool FilePath::exists() const noexcept
{
	if (!m_checkedExists)
	{
		m_exists = std::filesystem::exists(getPath());
		m_checkedExists = true;
	}

	return m_exists;
}

inline bool FilePath::recheckExists() const
{
	m_checkedExists = false;
	return exists();
}

inline bool FilePath::isDirectory() const
{
	if (!m_checkedIsDirectory)
	{
		m_isDirectory = std::filesystem::is_directory(getPath());
		m_checkedIsDirectory = true;
	}

	return m_isDirectory;
}

inline bool FilePath::isAbsolute() const
{
	return m_path.is_absolute();
}

inline bool FilePath::isValid() const
{
	auto it = m_path.begin();

	if (isAbsolute() && m_path.has_root_path())
	{
		std::string root = m_path.root_path().string();
		std::string current;
		while (current.size() < root.size())
		{
			current += it->string();
			it++;
		}
	}

	// Check each path component for characters invalid on Windows
	for (; it != m_path.end(); ++it)
	{
		const std::string s = it->string();
		if (s.empty() || s == "." || s == "..")
			continue;
		for (char c : s)
			if (c < 0x20 || std::string_view("\"*:<>?|/\\").find(c) != std::string_view::npos)
				return false;
	}

	return true;
}

inline FilePath FilePath::getParentDirectory() const
{
	FilePath parentDirectory(m_path.parent_path().string());

	if (!parentDirectory.empty())
	{
		parentDirectory.m_checkedIsDirectory = true;
		parentDirectory.m_isDirectory = true;

		if (m_checkedExists && m_exists)
		{
			parentDirectory.m_checkedExists = true;
			parentDirectory.m_exists = true;
		}
	}

	return parentDirectory;
}

inline FilePath& FilePath::makeAbsolute()
{
	m_path = std::filesystem::absolute(getPath());
	return *this;
}

inline FilePath FilePath::getAbsolute() const
{
	FilePath path(*this);
	path.makeAbsolute();
	return path;
}

inline FilePath& FilePath::makeCanonical()
{
	if (m_canonicalized || !exists())
	{
		return *this;
	}
	try
	{
		m_path = std::filesystem::canonical(getPath());
		m_canonicalized = true;
		return *this;
	}
	catch (const std::filesystem::filesystem_error &e)
	{
		LOG_ERROR_STREAM(<< e.what());
		return *this;
	}
}

inline FilePath FilePath::getCanonical() const
{
	FilePath path(*this);
	path.makeCanonical();
	return path;
}

inline std::vector<FilePath> FilePath::expandEnvironmentVariables() const
{
	std::vector<FilePath> paths;
	std::string text = str();

	// Expand ${VAR} and %VAR% references by elementary scanning (leftmost reference wins, matching the old
	// `\$\{([^}]+)\}|%([^%]+)%` regex) -- no std::regex. Repeat until none remain; an undefined variable
	// aborts and yields no paths, as before.
	for (;;)
	{
		std::size_t start = std::string::npos;
		std::size_t length = 0;
		std::string name;

		// ${VAR}: leftmost "${" up to the next "}" with >=1 char between ('}' is a distinct terminator).
		if (const std::size_t open = text.find("${"); open != std::string::npos)
		{
			if (const std::size_t close = text.find('}', open + 2);
				close != std::string::npos && close > open + 2)
			{
				start = open;
				length = close - open + 1;
				name = text.substr(open + 2, close - open - 2);
			}
		}

		// %VAR%: leftmost pair of '%' with >=1 char between; skip empty "%%" (both delimiters are '%').
		for (std::size_t open = text.find('%'); open != std::string::npos && open < start;)
		{
			const std::size_t close = text.find('%', open + 1);
			if (close == std::string::npos)
			{
				break;
			}
			if (close > open + 1)
			{
				start = open;
				length = close - open + 1;
				name = text.substr(open + 1, close - open - 1);
				break;
			}
			open = close;	 // empty "%%" -> the second '%' begins the next candidate
		}

		if (start == std::string::npos)
		{
			break;
		}

		const char* const value = std::getenv(name.c_str());
		if (value == nullptr)
		{
			LOG_ERROR_STREAM(<< name << " is not an environment variable in: " << text);
			return paths;
		}
		text.replace(start, length, value);
	}

	for (const std::string& str: utility::splitToVector(text, getEnvironmentVariablePathSeparator()))
	{
		if (str.size())
		{
			paths.push_back(FilePath(str));
		}
	}

	return paths;
}

inline FilePath& FilePath::makeRelativeTo(const FilePath& other)
{
	const std::filesystem::path a = this->getCanonical().getPath();
	const std::filesystem::path b = other.getCanonical().getPath();

	if (a.root_path() != b.root_path())
	{
		return *this;
	}

	auto itA = a.begin();
	auto itB = b.begin();

	// Check the bounds BEFORE dereferencing: when one path is a prefix of the
	// other (or they are equal), the lockstep walk reaches end() and the old
	// dereference-first order read past the end -- benign on libc++, a segfault
	// on libstdc++ (Linux).
	while (itA != a.end() && itB != b.end() && *itA == *itB)
	{
		itA++;
		itB++;
	}

	std::filesystem::path r;

	if (itB != b.end())
	{
		if (!std::filesystem::is_directory(b))
		{
			itB++;
		}

		for (; itB != b.end(); itB++)
		{
			r /= "..";
		}
	}

	for (; itA != a.end(); itA++)
	{
		r /= *itA;
	}

	if (r.empty())
	{
		r = "./";
	}

	m_path = r;
	return *this;
}


inline FilePath FilePath::getRelativeTo(const FilePath& other) const
{
	FilePath path(*this);
	path.makeRelativeTo(other);
	return path;
}

inline FilePath& FilePath::concatenate(const FilePath& other)
{
	m_path /= other.getPath().relative_path();
	m_exists = false;
	m_checkedExists = false;
	m_isDirectory = false;
	m_checkedIsDirectory = false;
	m_canonicalized = false;

	return *this;
}

inline FilePath FilePath::getConcatenated(const FilePath& other) const
{
	FilePath path(*this);
	path.concatenate(other);
	return path;
}

inline FilePath& FilePath::concatenate(const char other[])
{
	m_path /= std::filesystem::path(other).relative_path();
	m_exists = false;
	m_checkedExists = false;
	m_isDirectory = false;
	m_checkedIsDirectory = false;
	m_canonicalized = false;

	return *this;
}

inline FilePath FilePath::getConcatenated(const char other[]) const
{
	FilePath path(*this);
	path.concatenate(other);
	return path;
}

inline FilePath FilePath::getLowerCase() const
{
	// ASCII lowercasing (paths are effectively ASCII; case-insensitive path matching on Windows is ASCII).
	// Replaces utility::toLowerCase -- a Qt/locale-based helper -- so FilePath carries no Qt dependency.
	std::string lower = str();
	for (char& c: lower)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return FilePath(lower);
}

inline bool FilePath::contains(const FilePath& other) const
{
	if (!isDirectory())
	{
		return false;
	}

	std::filesystem::path dir = getPath();
	const std::filesystem::path& dir2 = other.m_path;

	if (dir.filename() == ".")
	{
		dir.remove_filename();
	}

	auto it = dir.begin();
	auto it2 = dir2.begin();

	while (it != dir.end())
	{
		if (it2 == dir2.end())
		{
			return false;
		}

		if (*it != *it2)
		{
			return false;
		}

		it++;
		it2++;
	}

	return true;
}

inline std::string FilePath::str() const
{
	return m_path.generic_string();
}

inline std::string FilePath::fileName() const
{
	return m_path.filename().generic_string();
}

inline std::string FilePath::extension() const
{
	return m_path.extension().generic_string();
}

inline FilePath FilePath::withoutExtension() const
{
	std::filesystem::path tmpPath(getPath());
	return FilePath(tmpPath.replace_extension().string());
}

inline FilePath FilePath::replaceExtension(const std::string& extension) const
{
	std::filesystem::path tmpPath(getPath());
	return FilePath(tmpPath.replace_extension(extension).string());
}

inline bool FilePath::hasExtension(const std::vector<std::string>& extensions) const
{
	const std::string e = extension();
	for (const std::string& ext: extensions)
	{
		if (e == ext)
		{
			return true;
		}
	}
	return false;
}

inline FilePath& FilePath::operator=(const FilePath& other)
{
	m_path = other.getPath();
	m_exists = other.m_exists;
	m_checkedExists = other.m_checkedExists;
	m_isDirectory = other.m_isDirectory;
	m_checkedIsDirectory = other.m_checkedIsDirectory;
	m_canonicalized = other.m_canonicalized;
	return *this;
}

inline FilePath& FilePath::operator=(FilePath&& other)
{
	m_path = std::move(other.m_path);
	m_exists = other.m_exists;
	m_checkedExists = other.m_checkedExists;
	m_isDirectory = other.m_isDirectory;
	m_checkedIsDirectory = other.m_checkedIsDirectory;
	m_canonicalized = other.m_canonicalized;
	return *this;
}

inline bool FilePath::operator==(const FilePath& other) const
{
	if (exists() && other.exists())
	{
		return std::filesystem::equivalent(getPath(), other.getPath());
	}

	return m_path.compare(other.getPath()) == 0;
}

inline bool FilePath::operator!=(const FilePath& other) const
{
	return !(*this == other);
}

inline bool FilePath::operator<(const FilePath& other) const
{
	return m_path.compare(other.getPath()) < 0;
}
