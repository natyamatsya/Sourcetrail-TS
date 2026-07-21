// Inline implementations for SourceGroupSettingsWithExcludeFilters.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

// Family-internal includes stay unguarded: same module either way; include guards
// + inl-after-class ordering make the cross-references resolve in both builds.
#include "ProjectSettings.h"

// Cross-module/std/GMF-linked deps: the wrapper supplies these via imports or its GMF.
#ifndef SRCTRL_MODULE_PURVIEW
#include <QRegularExpression>
#include <QString>
#include "FilePathFilter.h"
#include "FileSystem.h"
#include "utility.h"
#include "utilityFile.h"
#endif

inline std::vector<std::string> SourceGroupSettingsWithExcludeFilters::getExcludeFilterStrings() const
{
	return m_excludeFilters;
}

inline std::vector<FilePathFilter> SourceGroupSettingsWithExcludeFilters::getExcludeFiltersExpandedAndAbsolute() const
{
	return getFiltersExpandedAndAbsolute(getExcludeFilterStrings());
}

inline void SourceGroupSettingsWithExcludeFilters::setExcludeFilterStrings(
	const std::vector<std::string>& excludeFilters)
{
	m_excludeFilters = excludeFilters;
}

inline bool SourceGroupSettingsWithExcludeFilters::equals(const SourceGroupSettingsBase* other) const
{
	const SourceGroupSettingsWithExcludeFilters* otherPtr =
		dynamic_cast<const SourceGroupSettingsWithExcludeFilters*>(other);

	return (otherPtr && utility::isPermutation(m_excludeFilters, otherPtr->m_excludeFilters));
}

inline void SourceGroupSettingsWithExcludeFilters::load(const ConfigManager* config, const std::string& key)
{
	setExcludeFilterStrings(config->getValuesOrDefaults(
		key + "/exclude_filters/exclude_filter", std::vector<std::string>()));
}

inline void SourceGroupSettingsWithExcludeFilters::save(ConfigManager* config, const std::string& key)
{
	config->setValues(key + "/exclude_filters/exclude_filter", getExcludeFilterStrings());
}

inline std::vector<FilePathFilter> SourceGroupSettingsWithExcludeFilters::getFiltersExpandedAndAbsolute(
	const std::vector<std::string>& filterStrings) const
{
	const FilePath projectDirectoryPath = getProjectSettings()->getProjectDirectoryPath();

	std::vector<FilePathFilter> result;

	for (const std::string& filterString: filterStrings)
	{
		if (!filterString.empty())
		{
			const size_t wildcardPos = filterString.find("*");
			if (wildcardPos != std::string::npos)
			{
				QString qFilter = QString::fromStdString(filterString);
				QRegularExpressionMatch match = QRegularExpression(QStringLiteral("[\\\\/]")).match(qFilter);
				if (match.hasMatch() && match.capturedStart(0) < int(wildcardPos))
				{
					const FilePath p = utility::getExpandedAndAbsolutePath(
						FilePath(qFilter.left(match.capturedStart(0)).toStdString()), projectDirectoryPath);
					std::set<FilePath> symLinkPaths = FileSystem::getSymLinkedDirectories(p);
					symLinkPaths.insert(p);

					std::string suffix = qFilter.mid(match.capturedEnd(0)).toStdString();
					utility::append(
						result,
						utility::convert<FilePath, FilePathFilter>(
							utility::toVector(symLinkPaths), [&suffix](const FilePath& filePath) {
								return FilePathFilter(filePath.str() + "/" + suffix);
							}));
				}
				else
				{
					result.push_back(FilePathFilter(filterString));
				}
			}
			else
			{
				const FilePath p = utility::getExpandedAndAbsolutePath(
					FilePath(filterString), projectDirectoryPath);
				const bool isFile = p.exists() && !p.isDirectory();

				std::set<FilePath> symLinkPaths = FileSystem::getSymLinkedDirectories(p);
				symLinkPaths.insert(p);

				utility::append(
					result,
					utility::convert<FilePath, FilePathFilter>(
						utility::toVector(symLinkPaths), [isFile](const FilePath& filePath) {
							return FilePathFilter(filePath.str() + (isFile ? "" : "**"));
						}));
			}
		}
	}

	return result;
}
