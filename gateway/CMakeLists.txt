
add_executable(cloudshield
    src/core/main.cpp
    src/network/tcp_server.cpp
    src/http/http_request.cpp
)

target_include_directories(cloudshield
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(http_request_test
    tests/unit/http_request_test.cpp
    src/http/http_request.cpp
)

target_include_directories(http_request_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_testing()

add_test(
    NAME HttpRequestParserTests
    COMMAND http_request_test
)
