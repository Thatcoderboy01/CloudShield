#include "router/http_router.hpp"

#include <iostream>
#include <string>

namespace {

int tests_passed = 0;
int tests_failed = 0;

void expect(bool condition, const std::string& test_name) {
    if (condition) {
        ++tests_passed;
        std::cout << "[PASS] " << test_name << '\n';
    } else {
        ++tests_failed;
        std::cerr << "[FAIL] " << test_name << '\n';
    }
}

cloudshield::http::HttpRequest makeRequest(
    const std::string& method,
    const std::string& target
) {
    cloudshield::http::HttpRequest request;
    request.method = method;
    request.target = target;
    request.version = "HTTP/1.1";
    return request;
}

}  // namespace

int main() {
    using cloudshield::router::routeRequest;

    {
        const auto response = routeRequest(makeRequest("GET", "/"));

        expect(response.status_code == 200,
               "GET / returns 200");
        expect(response.body.find("CloudShield Gateway") != std::string::npos,
               "GET / returns gateway message");
    }

    {
        const auto response = routeRequest(makeRequest("GET", "/health"));

        expect(response.status_code == 200,
               "GET /health returns 200");
        expect(response.content_type.find("application/json") !=
                   std::string::npos,
               "GET /health returns JSON content type");
        expect(response.body.find("\"status\":\"ok\"") != std::string::npos,
               "GET /health returns healthy status");
    }

    {
        const auto response = routeRequest(makeRequest("GET", "/unknown"));

        expect(response.status_code == 404,
               "Unknown route returns 404");
    }

    {
        const auto response = routeRequest(makeRequest("POST", "/health"));

        expect(response.status_code == 405,
               "Unsupported method returns 405");
    }

    std::cout << "\nPassed: " << tests_passed
              << ", Failed: " << tests_failed << '\n';

    return tests_failed == 0 ? 0 : 1;
}