
#pragma once

#include "http/http_request.hpp"

#include <cstdint>
#include <string>

namespace cloudshield::proxy {

enum class ConnectStatus {
    Connected,
    InProgress,
    Failed
};

struct ConnectResult {
    ConnectStatus status = ConnectStatus::Failed;
    int fd = -1;
    int error_code = 0;
};

ConnectResult startBackendConnection(
    const char* address,
    std::uint16_t port
);

std::string buildBackendRequest(
    const cloudshield::http::HttpRequest& request
);

}  // namespace cloudshield::proxy
