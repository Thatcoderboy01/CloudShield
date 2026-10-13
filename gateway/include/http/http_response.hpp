#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace cloudshield::http {

struct HttpResponse {
    int status_code = 200;
    std::string reason_phrase = "OK";
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::string connection = "close";
};

std::string serializeHttpResponse(const HttpResponse& response);

HttpResponse makeErrorResponse(
    int status_code,
    std::string_view message
);

}  // namespace cloudshield::http