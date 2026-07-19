add_library(AidKit_lib
	aidkit/enum_class.hpp
	aidkit/enum_class.cpp

	aidkit/thread_shared.hpp
	aidkit/thread_shared.cpp
	
	aidkit/qt/Strings.hpp
	aidkit/qt/Strings.cpp
)
setDefaultTargetOptions(AidKit_lib)

target_include_directories(AidKit_lib
	PUBLIC
		.
)
target_link_libraries(AidKit_lib
	PUBLIC
		External_lib_qt
)

# Phase 1 of the C++20-modules migration (context/DESIGN_INDEXER_MODULARIZATION.md): also build AidKit
# as the module `aidkit`. The .cpp TUs above still compile (they #include the headers as ordinary,
# non-purview TUs, so SRCTRL_EXPORT is empty there); the wrapper supplies the exported module
# interface. Consumers can `import aidkit;` or keep #including the (header-only) headers -- both work,
# because everything AidKit exports is a template or an inline function.
if(SOURCETRAIL_CXX_MODULES_ENABLED)
	# PUBLIC so consumers in other targets (e.g. AidKit_test) can `import aidkit;` -- a PRIVATE module
	# file set is visible only inside its own target.
	target_sources(AidKit_lib
		PUBLIC
			FILE_SET CXX_MODULES FILES aidkit.cppm)
	if(SOURCETRAIL_CXX_IMPORT_STD)
		# The wrapper `import std;`s, so this target consumes the toolchain std module.
		set_target_properties(AidKit_lib PROPERTIES CXX_MODULE_STD ON)
	endif()
endif()

