
#pragma once

#include "http/http_request.hpp"
#include "http/http_response.hpp"

namespace cloudshield::router {

cloudshield::http::HttpResponse routeRequest(
    const cloudshield::http::HttpRequest& request
);

}  // namespace cloudshield::router
