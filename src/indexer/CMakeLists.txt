add_executable(Sourcetrail_indexer
	main.cpp
)
setDefaultTargetOptions(Sourcetrail_indexer)

set_target_properties(Sourcetrail_indexer
	PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app/"
		PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/app/"
		OUTPUT_NAME              sourcetrail_indexer
)

target_link_libraries(Sourcetrail_indexer
	PRIVATE
		# Headless: the indexer needs only the base lib + the Cxx language package. Its shared bootstrap
		# (setupApp, QtCore-only) lives in lib now, so it no longer links Qt Widgets via lib_gui.
		Sourcetrail_lib_core
		$<$<BOOL:${BUILD_CXX_LANGUAGE_PACKAGE}>:Sourcetrail_lib_cxx>
)

# utilityString.inl pulls <QString> (inline locale functions), and Qt's `emit` macro breaks
# clang's Sema.h members in this clang-including TU. Same fix as the app target.
target_compile_definitions(Sourcetrail_indexer PRIVATE QT_NO_EMIT)

# Module build: main.cpp is the Phase 4 "pure importer" milestone -- it imports the modularized
# layers (srctrl.file/settings/logging/indexer/interprocess/cxx) instead of #including their
# headers. Same mechanics as the test target's importer TUs: define the toggle and scan only the
# file that imports (project-wide scanning is off; stdexec breaks clang-scan-deps).
if(SOURCETRAIL_CXX_MODULES_ENABLED)
	target_compile_definitions(Sourcetrail_indexer PRIVATE SRCTRL_MODULE_BUILD)
	set_source_files_properties(main.cpp PROPERTIES CXX_SCAN_FOR_MODULES ON)
endif()

# The recursive C++ AST visitor (CxxAstVisitor over clang's RecursiveASTVisitor)
# descends one frame per AST node. Pathologically nested code overflows the
# default 1 MiB thread stack on Windows — e.g. clang's own SemaARM.cpp intrinsic
# tables reach ~1240 nesting levels (0xC00000FD STACK_OVERFLOW). The parse runs
# on the indexer process's main thread, so reserve a large main-thread stack
# (address space only; pages commit lazily). Clang itself survives such input by
# running Sema on an enlarged stack (clang::runWithSufficientStackSpace).
if(MSVC)
	target_link_options(Sourcetrail_indexer PRIVATE "/STACK:268435456") # 256 MiB
elseif(APPLE)
	target_link_options(Sourcetrail_indexer PRIVATE "-Wl,-stack_size,0x10000000") # 256 MiB
endif()
# On Linux the main-thread stack grows dynamically to the ulimit (typically
# 8 MiB+), which covers the observed depth.
