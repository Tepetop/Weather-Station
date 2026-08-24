cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type. Tutaj przełączamy pomiedzy debug/release i NODE_ID (0 lub 1)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME OutdoorUnit)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

set(OUTDOOR_NODE_ID "1" CACHE STRING "Outdoor station node ID (0=SI7021/BMP280/TSL2561, 1=BME280)")
set_property(CACHE OUTDOOR_NODE_ID PROPERTY STRINGS 0 1)
if(NOT OUTDOOR_NODE_ID MATCHES "^[01]$")
    message(FATAL_ERROR "OUTDOOR_NODE_ID must be 0 or 1")
endif()
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NODE_ID=${OUTDOOR_NODE_ID}U)
message(STATUS "Outdoor node ID: ${OUTDOOR_NODE_ID}")

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    Core/Src/Station/ws_protocol.c
    Core/Src/Sensors/bme280.c
    Core/Src/Sensors/bmp280.c
    Core/Src/Sensors/si7021.c
    Core/Src/Sensors/TSL2561.c
    Core/Src/Sensors/NRF24L01.c
    Core/Src/Station/measurement.c
    Core/Src/Station/outdoor_station.c
    Core/Src/Station/debug_log.c
    Core/Src/Station/power_mgr.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    Core/Inc
    Core/Inc/Sensors
    Core/Inc/Station
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
