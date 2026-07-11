# Standalone build for the sqlpp23 bookmark-storage proof-of-concept.
#
# This is intentionally NOT wired into the main Sourcetrail build. Configure it
# on its own, pointing at a vcpkg install tree that provides sqlpp23:
#
#   cmake -S . -B build -G Ninja \
#       -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
#       -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake \
#       -DVCPKG_TARGET_TRIPLET=x64-arm64-linux-windows-osx-static-md \
#       -DVCPKG_OVERLAY_TRIPLETS=<repo>/vcpkg-triplets \
#       -DVCPKG_INSTALLED_DIR=<install-root> -DVCPKG_MANIFEST_MODE=OFF
#   cmake --build build && ./build/bookmark_sqlpp23_poc
cmake_minimum_required(VERSION 3.28)
project(bookmark_sqlpp23_poc CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Sqlpp23 CONFIG REQUIRED COMPONENTS SQLite3)

add_executable(bookmark_sqlpp23_poc
  poc_main.cpp
  SqliteBookmarkStorageSqlpp23.cpp
  SqliteStorageMetaSqlpp23.cpp)
target_link_libraries(bookmark_sqlpp23_poc PRIVATE sqlpp23::sqlite3)

add_executable(index_sqlpp23_poc
  index_poc_main.cpp
  SqliteIndexStorageSqlpp23.cpp)
target_link_libraries(index_sqlpp23_poc PRIVATE sqlpp23::sqlite3)
