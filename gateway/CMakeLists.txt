add_executable(cloudshield
    src/core/main.cpp
    src/network/tcp_server.cpp
    src/http/http_request.cpp
    src/http/http_response.cpp
    src/router/http_router.cpp
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

add_executable(http_response_test
    tests/unit/http_response_test.cpp
    src/http/http_response.cpp
)

target_include_directories(http_response_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_testing()

add_test(
    NAME HttpRequestParserTests
    COMMAND http_request_test
)

add_test(
    NAME HttpResponseTests
    COMMAND http_response_test
)

add_executable(http_router_test
    tests/unit/http_router_test.cpp
    src/router/http_router.cpp
    src/http/http_response.cpp
)

target_include_directories(http_router_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_test(
    NAME HttpRouterTests
    COMMAND http_router_test
)