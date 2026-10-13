#include "proxy/backend_response.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    using cloudshield::proxy::BackendParseStatus;
    using cloudshield::proxy::parseBackendResponse;

    // Valid response with a body and a custom header.
    const std::string valid =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 11\r\n"
        "X-Test: passed\r\n"
        "\r\n"
        "{\"ok\":true}";

    const auto complete = parseBackendResponse(valid);

    assert(complete.status == BackendParseStatus::Complete);
    assert(complete.response.status_code == 200);
    assert(complete.response.content_type == "application/json");
    assert(complete.response.body == "{\"ok\":true}");
    assert(complete.response.headers.at("X-Test") == "passed");

    // Partial response should wait for more bytes.
    const auto incomplete = parseBackendResponse(
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nab"
    );

    assert(incomplete.status == BackendParseStatus::Incomplete);

    // Malformed status line should be rejected.
    const auto malformed = parseBackendResponse(
        "NOT-HTTP\r\nContent-Length: 0\r\n\r\n"
    );

    assert(malformed.status == BackendParseStatus::BadResponse);

    std::cout << "Backend response parser tests passed\n";
    return 0;
}