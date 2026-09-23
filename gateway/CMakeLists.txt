add_executable(cloudshield
    src/core/main.cpp
)

target_include_directories(cloudshield
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
