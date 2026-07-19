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
		Sourcetrail_lib
		# setupApp.h (shared main bootstrap) lives in lib_gui/platform; the indexer used it transitively via
		# lib's old umbrella. lib is the base layer now, so name lib_gui here (as before, just explicit).
		Sourcetrail_lib_gui
		$<$<BOOL:${BUILD_CXX_LANGUAGE_PACKAGE}>:Sourcetrail_lib_cxx>
)

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
