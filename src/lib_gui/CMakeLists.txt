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
