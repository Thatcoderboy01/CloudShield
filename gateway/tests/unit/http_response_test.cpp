
#include "http/http_response.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int tests_passed = 0;
int tests_failed = 0;

void expect(
    bool condition,
    std::string_view test_name
) {
    if (condition) {
        ++tests_passed;
        std::cout << "[PASS] " << test_name << '\n';
    } else {
        ++tests_failed;
        std::cerr << "[FAIL] " << test_name << '\n';
    }
}

void test200OkResponse() {
    cloudshield::http::HttpResponse response;
    response.status_code = 200;
    response.reason_phrase = "OK";
    response.body = "Hello CloudShield";

    const std::string result =
        cloudshield::http::serializeHttpResponse(response);

    expect(
        result.find("HTTP/1.1 200 OK\r\n") == 0,
        "200 OK status line"
    );
}

void test404NotFoundResponse() {
    const auto response =
        cloudshield::http::makeErrorResponse(
            404,
            "Route not found."
        );

    const std::string result =
        cloudshield::http::serializeHttpResponse(response);

    expect(
        result.find("HTTP/1.1 404 Not Found\r\n") == 0,
        "404 Not Found status line"
    );
}

void testContentLength() {
    cloudshield::http::HttpResponse response;
    response.body = "Hello";

    const std::string result =
        cloudshield::http::serializeHttpResponse(response);

    expect(
        result.find("Content-Length: 5\r\n") !=
            std::string::npos,
        "Content-Length matches body size"
    );
}

void testHeadersAndBody() {
    cloudshield::http::HttpResponse response;
    response.content_type = "application/json";
    response.body = R"({"status":"ok"})";

    const std::string result =
        cloudshield::http::serializeHttpResponse(response);

    expect(
        result.find("Content-Type: application/json\r\n") !=
            std::string::npos,
        "Content-Type header"
    );

    const std::string body_separator = "\r\n\r\n";
    const auto separator_position =
        result.find(body_separator);

    expect(
        separator_position != std::string::npos &&
        result.substr(
            separator_position + body_separator.size()
        ) == response.body,
        "Response body follows header separator"
    );
}

void testErrorResponseConnectionClose() {
    const auto response =
        cloudshield::http::makeErrorResponse(
            400,
            "Malformed HTTP request."
        );

    const std::string result =
        cloudshield::http::serializeHttpResponse(response);

    expect(
        result.find("Connection: close\r\n") !=
            std::string::npos,
        "Error response closes connection"
    );

    expect(
        result.find("Malformed HTTP request.") !=
            std::string::npos,
        "Error response contains message"
    );
}

}  // namespace

int main() {
    test200OkResponse();
    test404NotFoundResponse();
    testContentLength();
    testHeadersAndBody();
    testErrorResponseConnectionClose();

    std::cout << "\nResults: "
              << tests_passed << " passed, "
              << tests_failed << " failed.\n";

    return tests_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
