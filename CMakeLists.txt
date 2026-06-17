cmake_minimum_required(VERSION 3.20)
project(Atlas VERSION 0.1.0 LANGUAGES CXX)

# Enforce Modern C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable standard compiler warnings for project health
if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

include(FetchContent)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# 1. nlohmann/json: For JSON file handling and API communication
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# 2. standalone Asio: For asynchronous networking and event loops
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

# Create an interface target that exposes Asio's nested headers and standalone macro
add_library(asio_standalone INTERFACE)
target_include_directories(asio_standalone INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio_standalone INTERFACE ASIO_STANDALONE)

# Explicitly define the Windows target version for Asio (Windows 10/11)
if(WIN32)
  target_compile_definitions(asio_standalone INTERFACE _WIN32_WINNT=0x0A00)
endif()

# ------------------------------------------------------------------------------
# Atlas Core Library
# ------------------------------------------------------------------------------

# Gather source files
file(GLOB_RECURSE ATLAS_SOURCES "src/core/*.cpp" "src/storage/*.cpp" "src/tools/*.cpp")

add_library(atlas_core ${ATLAS_SOURCES})

# Public includes (what consumers of the library see)
target_include_directories(atlas_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Link dependencies
target_link_libraries(atlas_core PUBLIC
    nlohmann_json::nlohmann_json
    asio_standalone
)

# ------------------------------------------------------------------------------
# Main Executable
# ------------------------------------------------------------------------------

add_executable(atlas src/main.cpp)
target_link_libraries(atlas PRIVATE atlas_core)
