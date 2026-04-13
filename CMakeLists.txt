cmake_minimum_required(VERSION 3.16)

project(fabric_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

add_executable(fabric main.cpp)
target_link_libraries(fabric PRIVATE SFML::Graphics SFML::Window SFML::System)

