cmake_minimum_required(VERSION 3.20)
project(Atlas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

# OpenSSL is optional: only the github_pr tool needs HTTPS. If it's not
# found (common on a fresh Windows machine with no OpenSSL install), the
# rest of Atlas still builds and runs fine - github_pr just self-disables
# with a clear error at runtime instead of failing the whole build.
# To enable it on Windows: `vcpkg install openssl:x64-windows` then
# re-run cmake with `-DCMAKE_TOOLCHAIN_FILE=<vcpkg root>/scripts/buildsystems/vcpkg.cmake`,
# or install OpenSSL and pass -DOPENSSL_ROOT_DIR=<path>.
find_package(OpenSSL QUIET)

include(FetchContent)

# 1. nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# 2. standalone Asio
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

add_library(asio_standalone INTERFACE)
target_include_directories(asio_standalone INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio_standalone INTERFACE ASIO_STANDALONE)
target_link_libraries(asio_standalone INTERFACE Threads::Threads)

if(WIN32)
  target_compile_definitions(asio_standalone INTERFACE _WIN32_WINNT=0x0A00)
endif()

# 3. cpp-httplib
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.0
)
FetchContent_MakeAvailable(httplib)

# Explicit Core Sources
set(ATLAS_SOURCES
    src/core/Application.cpp
    src/core/WorkspaceManager.cpp
    src/core/SessionManager.cpp
    src/core/SymbolIndexer.cpp
    src/storage/FileStorageManager.cpp
    src/agent/Agent.cpp
    src/agent/LLMClient.cpp
    src/api/APIServer.cpp
    src/tools/ToolManager.cpp
    src/tools/ReadFileTool.cpp
    src/tools/SearchSymbolTool.cpp
    src/tools/EditFileTool.cpp
    src/tools/WriteFileTool.cpp
    src/tools/GitTool.cpp
    src/tools/GitHubPRTool.cpp
)

add_library(atlas_core ${ATLAS_SOURCES})

target_include_directories(atlas_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(atlas_core PUBLIC
    nlohmann_json::nlohmann_json
    asio_standalone
    httplib::httplib
    Threads::Threads
)

if(OPENSSL_FOUND)
  target_link_libraries(atlas_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
  # GitHubPRTool needs HTTPS (api.github.com); cpp-httplib only compiles in
  # OpenSSL support (httplib::SSLClient) when this macro is defined before
  # its headers are seen.
  target_compile_definitions(atlas_core PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
else()
  message(STATUS "OpenSSL not found: building WITHOUT HTTPS support. "
                  "The github_pr tool will compile but always return an error at runtime. "
                  "See the comment near find_package(OpenSSL) above to enable it.")
endif()

if(WIN32)
  target_link_libraries(atlas_core PUBLIC ws2_32 crypt32)
endif()

add_executable(atlas src/main.cpp)
target_link_libraries(atlas PRIVATE atlas_core)

enable_testing()
