# Toolchain file for Homebrew LLVM clang on macOS.
#
# Homebrew LLVM 18+ libc++ headers emit ABI-tag-decorated symbols (ne210108)
# via _LIBCPP_HIDE_FROM_ABI.  Apple's system libc++.dylib does not export those
# symbols, and linking LLVM's static libc++.a instead breaks Qt's implicit
# sharing (COW) because Qt is built against Apple libc++.
#
# The fix: keep Apple's system libc++ at runtime (-stdlib=libc++ with no
# -nostdlib++) but suppress the ABI tag decoration at compile time via
# -D_LIBCPP_NO_ABI_TAG.  This makes LLVM clang emit the same undecorated
# symbols that Apple libc++.dylib exports, so everything links and runs with a
# single consistent libc++ ABI.  vcpkg must also be built with this flag (via
# this same chainload toolchain) so its archives match.

set(LLVM_PREFIX "/opt/homebrew/opt/llvm")

set(CMAKE_C_COMPILER   "${LLVM_PREFIX}/bin/clang"   CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "${LLVM_PREFIX}/bin/clang++" CACHE FILEPATH "C++ compiler")

# Use macOS system ar/ranlib — GNU ar (from binutils) produces archives that
# Apple's ld rejects.
set(CMAKE_AR     "/usr/bin/ar"     CACHE FILEPATH "ar")
set(CMAKE_RANLIB "/usr/bin/ranlib" CACHE FILEPATH "ranlib")

# Use Apple's system libc++ at runtime.
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")

# Provide the missing std::__1::__hash_memory symbol (removed from Apple's
# system libc++.dylib on macOS 26+) by pre-building a small stub archive and
# injecting it via linker flags.  The archive is built once at configure time
# using execute_process so it is available before any targets are created.
set(_compat_src "${CMAKE_CURRENT_LIST_DIR}/libcxx_compat.cpp")
set(_compat_obj "${CMAKE_BINARY_DIR}/libcxx_compat.o")
set(_compat_lib "${CMAKE_BINARY_DIR}/liblibcxx_compat.a")

if(NOT EXISTS "${_compat_lib}")
    message(STATUS "Building libcxx_compat stub: ${_compat_lib}")
    execute_process(
        COMMAND "${LLVM_PREFIX}/bin/clang++" -stdlib=libc++ -O2 -arch arm64
                -c "${_compat_src}" -o "${_compat_obj}"
        RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "Failed to compile libcxx_compat.cpp (exit ${_r})")
    endif()
    execute_process(
        COMMAND /usr/bin/ar rcs "${_compat_lib}" "${_compat_obj}"
        RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "Failed to create liblibcxx_compat.a (exit ${_r})")
    endif()
endif()

set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_compat_lib}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_compat_lib}")
