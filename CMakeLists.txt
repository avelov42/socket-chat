cmake_minimum_required(VERSION 2.8)
project(sik_radio)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_CXX_COMPILER "g++")

add_executable(sik_radio main.cpp)
add_executable(regex_test regex_test.cpp)
git remote add origin