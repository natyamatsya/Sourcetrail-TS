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
# Apple's ld rejects.  (This also lets CMake's `import std` support link its std-module archive.)
set(CMAKE_AR     "/usr/bin/ar"     CACHE FILEPATH "ar")
set(CMAKE_RANLIB "/usr/bin/ranlib" CACHE FILEPATH "ranlib")

# `import std` (SOURCETRAIL_CXX_IMPORT_STD): Homebrew LLVM ships libc++.modules.json under lib/c++/,
# where clang's `-print-file-name` (which CMake's import-std detection queries) does not find it.
# Point CMake at it explicitly so the toolchain probe succeeds.  Harmless when import std is off.
if(EXISTS "${LLVM_PREFIX}/lib/c++/libc++.modules.json")
    set(CMAKE_CXX_STDLIB_MODULES_JSON "${LLVM_PREFIX}/lib/c++/libc++.modules.json"
        CACHE FILEPATH "libc++ std module metadata (for import std)")
endif()

# Use Apple's system libc++ at runtime.
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")

# std::__1::__hash_memory is an out-of-line libc++ ABI function (out-of-line
# precisely so the algorithm may change between libc++ versions).  Some system
# libc++.dylib builds did not export it (observed on macOS 26); current ones do.
# Probe at configure time and inject the murmur2 stub archive ONLY when the
# system libc++ lacks the symbol.  Injecting it when the dylib provides one is
# actively harmful: the archive wins inside our binaries while other images
# bind the dylib's (different) algorithm — two std::hash<std::string> in one
# process, corrupting unordered containers (crashes in the test suite).
set(_compat_src "${CMAKE_CURRENT_LIST_DIR}/libcxx_compat.cpp")
set(_compat_obj "${CMAKE_BINARY_DIR}/libcxx_compat.o")
set(_compat_lib "${CMAKE_BINARY_DIR}/liblibcxx_compat.a")
set(_hash_probe_src "${CMAKE_BINARY_DIR}/hash_memory_probe.cpp")
set(_hash_probe_bin "${CMAKE_BINARY_DIR}/hash_memory_probe")

# Memoized like the archive below: an existing probe binary means "linked OK".
if(NOT EXISTS "${_hash_probe_bin}")
    file(WRITE "${_hash_probe_src}" "
#include <cstddef>
namespace std { inline namespace __1 { size_t __hash_memory(const void*, size_t) noexcept; } }
int main() { char c = 'x'; return static_cast<int>(std::__1::__hash_memory(&c, 1) & 1); }
")
    execute_process(
        COMMAND "${LLVM_PREFIX}/bin/clang++" -stdlib=libc++ -arch arm64
                "${_hash_probe_src}" -o "${_hash_probe_bin}"
        RESULT_VARIABLE _probe_r
        OUTPUT_QUIET ERROR_QUIET)
else()
    set(_probe_r 0)
endif()

if(_probe_r EQUAL 0)
    # System libc++ exports __hash_memory — do not inject the stub.
else()
    if(NOT EXISTS "${_compat_lib}")
        message(STATUS "System libc++ lacks __hash_memory; building libcxx_compat stub: ${_compat_lib}")
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
endif()
