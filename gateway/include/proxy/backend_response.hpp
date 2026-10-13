#pragma once

#include "http/http_response.hpp"

#include <string_view>

namespace cloudshield::proxy {

enum class BackendParseStatus {
    Complete,
    Incomplete,
    BadResponse
};

struct BackendParseResult {
    BackendParseStatus status = BackendParseStatus::Incomplete;
    cloudshield::http::HttpResponse response;
    std::string error;
};

BackendParseResult parseBackendResponse(std::string_view raw);

}  // namespace cloudshield::proxy