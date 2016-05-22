cmake_minimum_required(VERSION 2.8)
project(sik_radio)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(CMAKE_CXX_COMPILER "g++")

add_library(common common.cpp)
add_executable(player player.cpp)
add_executable(regex_test regex_test.cpp)
target_link_libraries(player common)
target_link_libraries(regex_test boost_regex)
target_link_libraries(player boost_regex)