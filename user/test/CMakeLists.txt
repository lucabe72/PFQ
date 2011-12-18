cmake_minimum_required(VERSION 2.4)

include_directories(../../kernel)
include_directories(../../user/c++)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra -std=c++0x")

# C++11 tests

add_executable(test-batch++ test-batch++.cpp)
