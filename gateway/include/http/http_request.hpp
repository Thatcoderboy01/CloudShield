
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace cloudshield::http {

enum class ParseStatus {
    Complete,
    Incomplete,
    BadRequest
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    HttpRequest request;
    std::string error;
};

ParseResult parseHttpRequest(std::string_view raw);

}  // namespace cloudshield::http
