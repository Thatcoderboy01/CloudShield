cmake_minimum_required(VERSION 3.20)

project(CloudShield
    VERSION 0.1.0
    DESCRIPTION "A High-Performance Secure API Gateway for Cloud-Native Applications"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_subdirectory(gateway)
