cmake_minimum_required(VERSION 3.20)
project(hello CXX)

add_executable(hello src/hello.cpp)
target_include_directories(hello PRIVATE include)
target_compile_definitions(hello PRIVATE GREETING="Hello")
