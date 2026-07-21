# Glob all sources except the CXX-only subdirectories, which are added
# conditionally below.
file(GLOB_RECURSE _lib_gui_base_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/platform/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/qt/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/qt/*.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/CombinedPathDetector.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/CombinedPathDetector.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/PathDetector.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/PathDetector.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/utility/utilityApp.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/utility/utilityApp.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/utility/utilityPathDetection.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/utility/utilityPathDetection.h"
)

# Exclude the CXX-only wizard content and path-detector subdirectory from the
# base glob — they are added under the BUILD_CXX_LANGUAGE_PACKAGE guard below.
list(FILTER _lib_gui_base_SOURCES EXCLUDE REGEX ".*/utility/path_detector/cxx_header/.*")
list(FILTER _lib_gui_base_SOURCES EXCLUDE REGEX ".*/QtProjectWizardContentPathCDB\\.(cpp|h)")
list(FILTER _lib_gui_base_SOURCES EXCLUDE REGEX ".*/QtProjectWizardContentPathCxxPch\\.(cpp|h)")
list(FILTER _lib_gui_base_SOURCES EXCLUDE REGEX ".*/QtProjectWizardContentPathsHeaderSearch\\.(cpp|h)")
list(FILTER _lib_gui_base_SOURCES EXCLUDE REGEX ".*/QtProjectWizardContentPathsIndexedHeaders\\.(cpp|h)")

add_library(Sourcetrail_lib_gui
	${CMAKE_CURRENT_BINARY_DIR}/licenses.h
	${_lib_gui_base_SOURCES}
)

# Derive include directories automatically from the base source tree.
set(_gui_include_dirs "")
foreach(_f ${_lib_gui_base_SOURCES})
	get_filename_component(_d "${_f}" DIRECTORY)
	list(APPEND _gui_include_dirs "${_d}")
endforeach()
list(REMOVE_DUPLICATES _gui_include_dirs)

target_include_directories(Sourcetrail_lib_gui
	PUBLIC
		${_gui_include_dirs}
		utility/path_detector
		utility
)

if (BUILD_CXX_LANGUAGE_PACKAGE)
	file(GLOB_RECURSE _lib_gui_cxx_SOURCES CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/cxx_header/*.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/cxx_header/*.h"
	)
	target_sources(Sourcetrail_lib_gui
		PRIVATE
			${_lib_gui_cxx_SOURCES}
			qt/project_wizard/content/path/QtProjectWizardContentPathCDB.cpp
			qt/project_wizard/content/path/QtProjectWizardContentPathCDB.h
			qt/project_wizard/content/path/QtProjectWizardContentPathCxxPch.cpp
			qt/project_wizard/content/path/QtProjectWizardContentPathCxxPch.h
			qt/project_wizard/content/paths/QtProjectWizardContentPathsHeaderSearch.cpp
			qt/project_wizard/content/paths/QtProjectWizardContentPathsHeaderSearch.h
			qt/project_wizard/content/paths/QtProjectWizardContentPathsIndexedHeaders.cpp
			qt/project_wizard/content/paths/QtProjectWizardContentPathsIndexedHeaders.h
	)
	target_include_directories(Sourcetrail_lib_gui
		PUBLIC
			utility/path_detector/cxx_header
	)
endif()


setDefaultTargetOptions(Sourcetrail_lib_gui)

set_target_properties(Sourcetrail_lib_gui
	PROPERTIES
		AUTOMOC ON
)

target_include_directories(Sourcetrail_lib_gui
	PUBLIC
		"${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(Sourcetrail_lib_gui
	PUBLIC
		Sourcetrail_lib_core
		# The Cxx project-wizard content (paths/) uses lib_cxx source-group headers directly. Previously this
		# came transitively via lib's umbrella link to lib_cxx; now that lib no longer links up, name it here.
		$<$<BOOL:${BUILD_CXX_LANGUAGE_PACKAGE}>:Sourcetrail_lib_cxx>
		External_lib_qt
		Sourcetrail_res_gui

		# Advanced docking (QtMainWindow): detach/reattach across monitors.
		External_lib_kddockwidgets

		# Qt<->stdexec bridge in qt/execution/ needs stdexec directly.
		STDEXEC::stdexec
)

# Module build: converted consumer TUs `import srctrl.*;` instead of #including the modularized
# headers (guards under SRCTRL_MODULE_BUILD; logging consumers define SRCTRL_LOGGING_VIA_IMPORT
# themselves -- see logging.h). Scan only the importing files (project-wide scanning is off;
# stdexec breaks clang-scan-deps -- which also makes stdexec-including TUs unconvertible, e.g.
# Schedulers.cpp/UiPost.cpp).
if(SOURCETRAIL_CXX_MODULES_ENABLED)
	# SRCTRL_MODULE_BUILD is PER-SOURCE ("this TU imports"): headers guard modularized fwd decls /
	# includes on it, and moc-generated TUs (which can never import) plus unconverted TUs keep the
	# classic textual view by not defining it.
	set(_srctrl_importing_tus
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/QtApplication.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/QtCoreApplication.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/QtTable.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/QtTooltip.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/CodeFocusHandler.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeArea.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeField.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeFile.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeFileList.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeFileSingle.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeFileTitleBar.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeFileTitleButton.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeNavigable.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeNavigator.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/code/QtCodeSnippet.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/dialog/QtListBox.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/dialog/QtLocationPicker.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/dialog/QtPathListBox.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/history/QtHistoryList.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/history/QtUndoRedo.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/search/QtAutocompletionList.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/search/QtSearchBar.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/search/QtSearchBarButton.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/search/QtSmartSearchBox.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/element/status/QtStatusBar.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/GraphFocusHandler.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/QtGraphicsView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/base/QtCountCircleItem.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/graph/QtGraphEdge.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/graph/QtGraphNode.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/graph/QtGraphNodeAccess.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/graph/QtGraphNodeBundle.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/graph/QtGraphNodeData.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/graph/QtGraphNodeExpandToggle.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/graph/QtGraphNodeGroup.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/graphics/graph/QtGraphNodeQualifier.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/network/QtIDECommunicationController.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/network/QtRequest.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/network/QtTcpWrapper.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/QtProjectWizard.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContent.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentCargoOptions.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentCustomCommand.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentCxxPchFlags.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentFlags.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentPreferences.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentProjectData.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentSelect.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentSourceGroupData.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentSwiftOptions.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/QtProjectWizardContentUnloadable.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/path/QtProjectWizardContentPath.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/path/QtProjectWizardContentPathCDB.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/path/QtProjectWizardContentPathCMakeFileAPI.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/path/QtProjectWizardContentPathCxxPch.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/paths/QtProjectWizardContentPaths.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/paths/QtProjectWizardContentPathsExclude.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/paths/QtProjectWizardContentPathsFrameworkSearch.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/paths/QtProjectWizardContentPathsFrameworkSearchGlobal.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/paths/QtProjectWizardContentPathsHeaderSearch.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/paths/QtProjectWizardContentPathsHeaderSearchGlobal.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/paths/QtProjectWizardContentPathsIndexedHeaders.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/project_wizard/content/paths/QtProjectWizardContentPathsSource.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/utility/QtColorSchemeWatcher.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/utility/QtContextMenu.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/utility/QtFileDialog.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/utility/QtFilesAndDirectoriesDialog.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/utility/QtHighlighter.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/utility/QtScreenshot.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/utility/QtScrollSpeedChangeListener.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/utility/QtUiSnapshot.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/utility/utilityQt.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtBookmarkButtonsView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtCodeView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtCompositeView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtCustomTrailView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtDialogView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtErrorView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtGraphView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtGraphViewStyleImpl.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtMainView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtRefreshView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtStatusView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtTabbedView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtTabsView.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtViewFactory.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/view/QtViewWidgetWrapper.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtActions.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtBookmarkCreator.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtIndexingProgressDialog.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtIndexingReportDialog.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtLicenseWindow.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtMainWindow.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtPreferencesWindow.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtSelectPathsDialog.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtStartScreen.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/qt/window/QtWindowBase.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/CombinedPathDetector.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/PathDetector.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/cxx_header/CxxFrameworkPathDetector.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/cxx_header/CxxHeaderPathDetector.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/cxx_header/CxxVs17ToLatestHeaderPathDetector.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/path_detector/cxx_header/utilityCxxHeaderDetection.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/utility/utilityPathDetection.cpp"
	)
	set_source_files_properties(${_srctrl_importing_tus} PROPERTIES CXX_SCAN_FOR_MODULES ON)
	set_property(SOURCE ${_srctrl_importing_tus} APPEND PROPERTY
		COMPILE_DEFINITIONS SRCTRL_MODULE_BUILD)
endif()
