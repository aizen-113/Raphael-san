cmake_minimum_required(VERSION 3.20)

project(Raphael VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(raphael
    src/main.cpp
    src/core/config.cpp
    src/core/assistant.cpp
    src/models/nim_client.cpp
    src/models/registry.cpp
    src/core/background_job.cpp
)

target_include_directories(raphael PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(raphael PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
)
