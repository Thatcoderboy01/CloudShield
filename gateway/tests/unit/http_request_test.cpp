
#include "http/http_request.hpp"

#include <cassert>
#include <iostream>
#include <string>

using cloudshield::http::ParseStatus;
using cloudshield::http::parseHttpRequest;

int main() {
    // Test 1: Valid GET
    {
        auto result = parseHttpRequest(
            "GET /health HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n"
        );

        assert(result.status == ParseStatus::Complete);
        assert(result.request.method == "GET");
        assert(result.request.target == "/health");
    }

    // Test 2: Valid POST
    {
        auto result = parseHttpRequest(
            "POST /api HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "hello"
        );

        assert(result.status == ParseStatus::Complete);
        assert(result.request.body == "hello");
    }

    // Test 3: Incomplete headers
    {
        auto result = parseHttpRequest(
            "GET / HTTP/1.1\r\nHost:"
        );

        assert(result.status == ParseStatus::Incomplete);
    }

    // Test 4: Missing Host
    {
        auto result = parseHttpRequest(
            "GET / HTTP/1.1\r\n\r\n"
        );

        assert(result.status == ParseStatus::BadRequest);
    }

    // Test 5: Incomplete body
    {
        auto result = parseHttpRequest(
            "POST / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 10\r\n"
            "\r\n"
            "hello"
        );

        assert(result.status == ParseStatus::Incomplete);
    }

    // Test 6: Oversized request
    {
        std::string raw(5000, 'x');

        auto result = parseHttpRequest(raw);

        assert(result.status == ParseStatus::BadRequest);
    }

    // Test 7: Unsupported Transfer-Encoding
    {
        auto result = parseHttpRequest(
            "POST / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
        );

        assert(result.status == ParseStatus::BadRequest);
    }

    std::cout << "All 7 HTTP parser tests passed!\n";
    return 0;
}
