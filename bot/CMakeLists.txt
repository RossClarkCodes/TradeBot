cmake_minimum_required(VERSION 3.16)
project(kraken_trading_bot VERSION 1.0.0 LANGUAGES CXX)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Find nlohmann_json
# Try find_package first (for vcpkg, conan, or system install)
find_package(nlohmann_json 3.9.0 QUIET)

if(NOT nlohmann_json_FOUND)
    # Try to find it as a header-only library
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
        PATHS
            /usr/local/include
            /usr/include
            /opt/homebrew/include
            ${CMAKE_SOURCE_DIR}/external
    )
    
    if(NLOHMANN_JSON_INCLUDE_DIR)
        message(STATUS "Found nlohmann/json.hpp at: ${NLOHMANN_JSON_INCLUDE_DIR}")
        set(NLOHMANN_JSON_INCLUDE_DIRS ${NLOHMANN_JSON_INCLUDE_DIR})
    else()
        message(FATAL_ERROR "nlohmann/json not found. Please install it:\n"
            "  macOS: brew install nlohmann-json\n"
            "  Ubuntu: sudo apt-get install nlohmann-json3-dev\n"
            "Or download json.hpp to external/nlohmann/json.hpp")
    endif()
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/state.cpp
    src/logger.cpp
    src/kraken_client.cpp
    src/strategy.cpp
    src/util.cpp
)

# Header files (for IDE support)
set(HEADERS
    src/config.hpp
    src/state.hpp
    src/logger.hpp
    src/kraken_client.hpp
    src/strategy.hpp
    src/util.hpp
)

# Create executable
add_executable(trading_bot ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(trading_bot PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

if(nlohmann_json_FOUND)
    target_link_libraries(trading_bot PRIVATE nlohmann_json::nlohmann_json)
else()
    target_include_directories(trading_bot PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
endif()

# Link libraries
target_link_libraries(trading_bot PRIVATE
    ${CURL_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Platform-specific settings
if(APPLE)
    # macOS may need additional frameworks
    find_library(SECURITY_FRAMEWORK Security)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    if(SECURITY_FRAMEWORK AND COREFOUNDATION_FRAMEWORK)
        target_link_libraries(trading_bot PRIVATE
            ${SECURITY_FRAMEWORK}
            ${COREFOUNDATION_FRAMEWORK}
        )
    endif()
endif()

# Install target
install(TARGETS trading_bot DESTINATION bin)
install(FILES config.json DESTINATION etc/trading_bot OPTIONAL)

# Print configuration summary
message(STATUS "")
message(STATUS "=== Kraken Trading Bot Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "CURL: ${CURL_LIBRARIES}")
message(STATUS "OpenSSL: ${OPENSSL_LIBRARIES}")
message(STATUS "")

