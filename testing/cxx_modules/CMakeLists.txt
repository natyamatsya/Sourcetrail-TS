cmake_minimum_required(VERSION 3.28)
project(cxx_modules_test CXX)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create an executable
add_executable(cxx_modules_test
    src/main.cpp
)

# Add a C++ module
target_sources(cxx_modules_test
    PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES
        FILES
            src/hello.cppm
)
