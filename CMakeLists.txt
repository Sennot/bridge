cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(bot_cps_bridge VERSION 0.1.0)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
	src/stealer.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${MOD_NAME} winhttp)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK. Set GEODE_SDK to your Geode SDK path.")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
