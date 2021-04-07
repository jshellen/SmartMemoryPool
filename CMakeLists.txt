cmake_minimum_required(VERSION 3.10)
project(SmartMemoryPool)

set(CMAKE_CXX_STANDARD 17)

add_library(SmartMemoryPool INTERFACE)

target_include_directories(SmartMemoryPool INTERFACE inc)

  