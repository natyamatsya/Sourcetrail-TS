# Toolchain file for Homebrew LLVM clang on macOS.
#
# Homebrew LLVM does not ship libc++.dylib — it only provides static archives.
# On macOS 26+ Apple's system libc++.dylib no longer exports __hash_memory and
# other symbols emitted by LLVM 18+ libc++ headers.  We therefore link libc++
# and libc++abi statically from the Homebrew LLVM installation so the build is
# fully self-contained.

set(LLVM_PREFIX "/opt/homebrew/opt/llvm")

set(CMAKE_C_COMPILER   "${LLVM_PREFIX}/bin/clang"   CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "${LLVM_PREFIX}/bin/clang++" CACHE FILEPATH "C++ compiler")

# Use macOS system ar/ranlib — GNU ar (from binutils) produces archives that
# Apple's ld rejects.
set(CMAKE_AR     "/usr/bin/ar"     CACHE FILEPATH "ar")
set(CMAKE_RANLIB "/usr/bin/ranlib" CACHE FILEPATH "ranlib")

# Use LLVM's own libc++ headers.
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++ -nostdlib++ -I${LLVM_PREFIX}/include/c++/v1")

# Link libc++ and libc++abi statically from the Homebrew LLVM tree, then pull
# in the system libSystem (required by libc++abi on macOS).
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-L${LLVM_PREFIX}/lib/c++ -lc++ -lc++abi -Wl,-rpath,${LLVM_PREFIX}/lib/c++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-L${LLVM_PREFIX}/lib/c++ -lc++ -lc++abi -Wl,-rpath,${LLVM_PREFIX}/lib/c++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT
    "-L${LLVM_PREFIX}/lib/c++ -lc++ -lc++abi -Wl,-rpath,${LLVM_PREFIX}/lib/c++")
