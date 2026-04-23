cmake_minimum_required(VERSION 3.16)
project(scout_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(scout_engine src/scout_engine.cpp)
