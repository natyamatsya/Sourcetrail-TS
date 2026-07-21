// Inline implementations for ApplicationSettings.h (included at its end). All definitions inline: the family
// is module-attached in the module build, and inline keeps ordinary mangling so classic TUs and
// the wrapper emit mergeable weak definitions (dual-build rule).

#pragma once

#ifndef SRCTRL_MODULE_PURVIEW
#include <utility>

#include "Logger.h"
#include "ResourcePaths.h"
#include "Status.h"
#include "UserPaths.h"
#include "utility.h"
#include "utilityFile.h"
#endif

inline const size_t ApplicationSettings::VERSION = 8;

inline std::shared_ptr<ApplicationSettings> ApplicationSettings::s_instance;

inline std::shared_ptr<ApplicationSettings> ApplicationSettings::getInstance()
{
	if (!s_instance)
	{
		s_instance = std::make_shared<ApplicationSettings>();
	}

	return s_instance;
}

inline bool ApplicationSettings::load(const FilePath& filePath, bool readOnly)
{
	const bool loaded = Settings::load(filePath, readOnly);
	if (!loaded)
		return false;

	if (!isValueDefined("application/font_name"))
		setFontName("Source Code Pro");
	if (!isValueDefined("application/font_size"))
		setFontSize(14);
	if (!isValueDefined("application/text_encoding"))
		setTextEncoding("UTF-8");
	if (!isValueDefined("application/use_animations"))
		setUseAnimations(true);
	if (!isValueDefined("application/builtin_types_in_graph"))
		setShowBuiltinTypesInGraph(false);

	if (!isValueDefined("application/hide_deprecated_in_graph"))
		setHideDeprecatedInGraph(false);
	if (!isValueDefined("application/directory_in_code_title"))
		setShowDirectoryInCodeFileTitle(false);
	if (!isValueDefined("application/scroll_speed"))
		setScrollSpeed(1.0f);
	if (!isValueDefined("code/tab_width"))
		setCodeTabWidth(4);

	return true;
}

inline bool ApplicationSettings::operator==(const ApplicationSettings& other) const
{
	return utility::isPermutation<FilePath>(getHeaderSearchPaths(), other.getHeaderSearchPaths()) &&
		utility::isPermutation<FilePath>(getFrameworkSearchPaths(), other.getFrameworkSearchPaths());
}

inline size_t ApplicationSettings::getMaxRecentProjectsCount()
{
	return 7;
}

inline std::string ApplicationSettings::getFontName() const
{
	return getValue<std::string>("application/font_name", "Source Code Pro");
}

inline void ApplicationSettings::setFontName(const std::string& fontName)
{
	setValue<std::string>("application/font_name", fontName);
}

inline int ApplicationSettings::getFontSize() const
{
	return getValue<int>("application/font_size", 14);
}

inline void ApplicationSettings::setFontSize(int fontSize)
{
	setValue<int>("application/font_size", fontSize);
}

inline std::string ApplicationSettings::getTextEncoding() const
{
	return getValue<std::string>("application/text_encoding", "UTF-8");
}

inline void ApplicationSettings::setTextEncoding(const std::string& textEncoding)
{
	setValue<std::string>("application/text_encoding", textEncoding);
}

inline bool ApplicationSettings::getUseAnimations() const
{
	return getValue<bool>("application/use_animations", true);
}

inline void ApplicationSettings::setUseAnimations(bool useAnimations)
{
	setValue<bool>("application/use_animations", useAnimations);
}

inline bool ApplicationSettings::getShowBuiltinTypesInGraph() const
{
	return getValue<bool>("application/builtin_types_in_graph", false);
}

inline void ApplicationSettings::setShowBuiltinTypesInGraph(bool showBuiltinTypes)
{
	setValue<bool>("application/builtin_types_in_graph", showBuiltinTypes);
}

inline bool ApplicationSettings::getHideDeprecatedInGraph() const
{
	return getValue<bool>("application/hide_deprecated_in_graph", false);
}

inline void ApplicationSettings::setHideDeprecatedInGraph(bool hideDeprecated)
{
	setValue<bool>("application/hide_deprecated_in_graph", hideDeprecated);
}

inline bool ApplicationSettings::getShowDirectoryInCodeFileTitle() const
{
	return getValue<bool>("application/directory_in_code_title", false);
}

inline void ApplicationSettings::setShowDirectoryInCodeFileTitle(bool showDirectory)
{
	setValue<bool>("application/directory_in_code_title", showDirectory);
}

inline std::string ApplicationSettings::getColorSchemeName() const
{
	return getValue<std::string>("application/color_scheme", "bright");
}

inline FilePath ApplicationSettings::getColorSchemePath() const
{
	return getColorSchemePath(getColorSchemeName());
}

inline FilePath ApplicationSettings::getColorSchemePath(const std::string& colorSchemeName) const
{
	FilePath defaultPath(ResourcePaths::getColorSchemesDirectoryPath().concatenate("bright.json"));
	FilePath path(
		ResourcePaths::getColorSchemesDirectoryPath().concatenate(colorSchemeName + ".json"));

	if (path != defaultPath && !path.exists())
	{
		return defaultPath;
	}

	return path;
}

inline void ApplicationSettings::setColorSchemeName(const std::string& colorSchemeName)
{
	setValue("application/color_scheme", colorSchemeName);
}

inline bool ApplicationSettings::getColorSchemeFollowsSystem() const
{
	return getValue<bool>("application/color_scheme_follows_system", false);
}

inline void ApplicationSettings::setColorSchemeFollowsSystem(bool followsSystem)
{
	setValue("application/color_scheme_follows_system", followsSystem);
}

inline std::string ApplicationSettings::getColorSchemeNameDark() const
{
	return getValue<std::string>("application/color_scheme_dark", "dark");
}

inline void ApplicationSettings::setColorSchemeNameDark(const std::string& colorSchemeName)
{
	setValue("application/color_scheme_dark", colorSchemeName);
}

inline int ApplicationSettings::getFontSizeMax() const
{
	return getValue<int>("application/font_size_max", 24);
}

inline void ApplicationSettings::setFontSizeMax(const int fontSizeMax)
{
	setValue<int>("application/font_size_max", fontSizeMax);
}

inline int ApplicationSettings::getFontSizeMin() const
{
	return getValue<int>("application/font_size_min", 4);
}

inline void ApplicationSettings::setFontSizeMin(const int fontSizeMin)
{
	setValue<int>("application/font_size_min", fontSizeMin);
}

inline int ApplicationSettings::getFontSizeStd() const
{
	return getValue<int>("application/font_size_std", 12);
}

inline void ApplicationSettings::setFontSizeStd(const int fontSizeStd)
{
	setValue<int>("application/font_size_std", fontSizeStd);
}

inline int ApplicationSettings::getWindowBaseWidth() const
{
	return getValue<int>("application/window_base_width", 500);
}

inline int ApplicationSettings::getWindowBaseHeight() const
{
	return getValue<int>("application/window_base_height", 500);
}

inline float ApplicationSettings::getScrollSpeed() const
{
	return getValue<float>("application/scroll_speed", 1.0f);
}

inline void ApplicationSettings::setScrollSpeed(float scrollSpeed)
{
	setValue<float>("application/scroll_speed", scrollSpeed);
}

inline bool ApplicationSettings::getGraphControlsVisible() const
{
	return getValue<bool>("application/graph_controls_visible", true);
}

inline void ApplicationSettings::setGraphControlsVisible(bool visible)
{
	setValue<bool>("application/graph_controls_visible", visible);
}

inline GroupType ApplicationSettings::getGraphGrouping() const
{
	return stringToGroupType(
		getValue<std::string>("application/graph_grouping", groupTypeToString(GroupType::NONE)));
}

inline void ApplicationSettings::setGraphGrouping(GroupType type)
{
	setValue<std::string>("application/graph_grouping", groupTypeToString(type));
}

inline int ApplicationSettings::getScreenAutoScaling() const
{
	return getValue<int>("screen/auto_scaling", 1);
}

inline void ApplicationSettings::setScreenAutoScaling(int autoScaling)
{
	setValue<int>("screen/auto_scaling", autoScaling);
}

inline float ApplicationSettings::getScreenScaleFactor() const
{
	return getValue<float>("screen/scale_factor", -1.0);
}

inline void ApplicationSettings::setScreenScaleFactor(float scaleFactor)
{
	setValue<float>("screen/scale_factor", scaleFactor);
}

inline bool ApplicationSettings::getLoggingEnabled() const
{
	return getValue<bool>("application/logging_enabled", true);
}

inline void ApplicationSettings::setLoggingEnabled(bool value)
{
	setValue<bool>("application/logging_enabled", value);
}

inline bool ApplicationSettings::getVerboseIndexerLoggingEnabled() const
{
	return getValue<bool>("application/verbose_indexer_logging_enabled", false);
}

inline void ApplicationSettings::setVerboseIndexerLoggingEnabled(bool value)
{
	setValue<bool>("application/verbose_indexer_logging_enabled", value);
}

inline FilePath ApplicationSettings::getLogDirectoryPath() const
{
	return FilePath(getValue<std::string>(
		"application/log_directory_path", UserPaths::getLogDirectoryPath().getAbsolute().str()));
}

inline void ApplicationSettings::setLogDirectoryPath(const FilePath& path)
{
	setValue<std::string>("application/log_directory_path", path.str());
}

inline void ApplicationSettings::setLogFilter(int mask)
{
	setValue<int>("application/log_filter", mask);
}

inline void ApplicationSettings::setStatusFilter(int mask)
{
	setValue<int>("application/status_filter", mask);
}

inline int ApplicationSettings::getStatusFilter() const
{
	return getValue<int>(
		"application/status_filter",
		std::to_underlying(StatusType::STATUS_INFO) | std::to_underlying(StatusType::STATUS_ERROR));
}

inline int ApplicationSettings::getLogFilter() const
{
	return getValue<int>("application/log_filter", Logger::LOG_WARNINGS | Logger::LOG_ERRORS);
}

inline int ApplicationSettings::getIndexerThreadCount() const
{
	return getValue<int>("indexing/indexer_thread_count", 0);
}

inline void ApplicationSettings::setIndexerThreadCount(const int count)
{
	setValue<int>("indexing/indexer_thread_count", count);
}

inline std::string ApplicationSettings::getMultiGroupFanOutMode() const
{
	return getValue<std::string>("indexing/multi_group_fan_out", "auto");
}

inline void ApplicationSettings::setMultiGroupFanOutMode(const std::string& mode)
{
	setValue<std::string>("indexing/multi_group_fan_out", mode);
}

inline std::vector<FilePath> ApplicationSettings::getHeaderSearchPaths() const
{
	return getPathValues("indexing/cxx/header_search_paths/header_search_path");
}

inline std::vector<FilePath> ApplicationSettings::getHeaderSearchPathsExpanded() const
{
	return utility::getExpandedPaths(getHeaderSearchPaths());
}

inline bool ApplicationSettings::setHeaderSearchPaths(const std::vector<FilePath>& headerSearchPaths)
{
	return setPathValues("indexing/cxx/header_search_paths/header_search_path", headerSearchPaths);
}

inline bool ApplicationSettings::getHasPrefilledHeaderSearchPaths() const
{
	return getValue<bool>("indexing/cxx/has_prefilled_header_search_paths", false);
}

inline void ApplicationSettings::setHasPrefilledHeaderSearchPaths(bool v)
{
	setValue<bool>("indexing/cxx/has_prefilled_header_search_paths", v);
}

inline std::vector<FilePath> ApplicationSettings::getFrameworkSearchPaths() const
{
	return getPathValues("indexing/cxx/framework_search_paths/framework_search_path");
}

inline std::vector<FilePath> ApplicationSettings::getFrameworkSearchPathsExpanded() const
{
	return utility::getExpandedPaths(getFrameworkSearchPaths());
}

inline bool ApplicationSettings::setFrameworkSearchPaths(const std::vector<FilePath>& frameworkSearchPaths)
{
	return setPathValues(
		"indexing/cxx/framework_search_paths/framework_search_path", frameworkSearchPaths);
}

inline bool ApplicationSettings::getHasPrefilledFrameworkSearchPaths() const
{
	return getValue<bool>("indexing/cxx/has_prefilled_framework_search_paths", false);
}

inline void ApplicationSettings::setHasPrefilledFrameworkSearchPaths(bool v)
{
	setValue<bool>("indexing/cxx/has_prefilled_framework_search_paths", v);
}

inline int ApplicationSettings::getCodeTabWidth() const
{
	return getValue<int>("code/tab_width", 4);
}

inline void ApplicationSettings::setCodeTabWidth(int codeTabWidth)
{
	setValue<int>("code/tab_width", codeTabWidth);
}

inline int ApplicationSettings::getCodeSnippetSnapRange() const
{
	return getValue<int>("code/snippet/snap_range", 4);
}

inline void ApplicationSettings::setCodeSnippetSnapRange(int range)
{
	setValue<int>("code/snippet/snap_range", range);
}

inline int ApplicationSettings::getCodeSnippetExpandRange() const
{
	return getValue<int>("code/snippet/expand_range", 3);
}

inline void ApplicationSettings::setCodeSnippetExpandRange(int range)
{
	setValue<int>("code/snippet/expand_range", range);
}

inline bool ApplicationSettings::getCodeViewModeSingle() const
{
	return getValue<bool>("code/view_mode_single", false);
}

inline void ApplicationSettings::setCodeViewModeSingle(bool enabled)
{
	setValue<bool>("code/view_mode_single", enabled);
}

inline std::vector<FilePath> ApplicationSettings::getRecentProjects() const
{
	std::vector<FilePath> recentProjects;
	std::vector<FilePath> loadedRecentProjects = getPathValues(
		"user/recent_projects/recent_project");

	for (const FilePath& project: loadedRecentProjects)
	{
		if (project.isAbsolute())
		{
			recentProjects.push_back(project);
		}
		else
		{
			recentProjects.push_back(UserPaths::getUserDataDirectoryPath().concatenate(project));
		}
	}
	return recentProjects;
}

inline bool ApplicationSettings::setRecentProjects(const std::vector<FilePath>& recentProjects)
{
	return setPathValues("user/recent_projects/recent_project", recentProjects);
}

inline bool ApplicationSettings::getSeenErrorHelpMessage() const
{
	return getValue<bool>("user/seen_error_help_message", false);
}

inline void ApplicationSettings::setSeenErrorHelpMessage(bool seen)
{
	setValue<bool>("user/seen_error_help_message", seen);
}

inline FilePath ApplicationSettings::getLastFilepickerLocation() const
{
	return FilePath(getValue<std::string>("user/last_filepicker_location", ""));
}

inline void ApplicationSettings::setLastFilepickerLocation(const FilePath& path)
{
	setValue<std::string>("user/last_filepicker_location", path.str());
}

inline float ApplicationSettings::getGraphZoomLevel() const
{
	return getValue<float>("user/graph_zoom_level", 1.0f);
}

inline void ApplicationSettings::setGraphZoomLevel(float zoomLevel)
{
	setValue<float>("user/graph_zoom_level", zoomLevel);
}

inline int ApplicationSettings::getPluginPort() const
{
	return getValue<int>("network/plugin_port", 6666);
}

inline void ApplicationSettings::setPluginPort(const int pluginPort)
{
	setValue<int>("network/plugin_port", pluginPort);
}

inline int ApplicationSettings::getSourcetrailPort() const
{
	return getValue<int>("network/sourcetrail_port", 6667);
}

inline void ApplicationSettings::setSourcetrailPort(const int sourcetrailPort)
{
	setValue<int>("network/sourcetrail_port", sourcetrailPort);
}

inline int ApplicationSettings::getControlsMouseBackButton() const
{
	return getValue<int>("controls/mouse_back_button", 0x8);
}

inline int ApplicationSettings::getControlsMouseForwardButton() const
{
	return getValue<int>("controls/mouse_forward_button", 0x10);
}

inline bool ApplicationSettings::getControlsGraphZoomOnMouseWheel() const
{
	return getValue<bool>("controls/graph_zoom_on_mouse_wheel", false);
}

inline void ApplicationSettings::setControlsGraphZoomOnMouseWheel(bool zoomingDefault)
{
	setValue<bool>("controls/graph_zoom_on_mouse_wheel", zoomingDefault);
}
