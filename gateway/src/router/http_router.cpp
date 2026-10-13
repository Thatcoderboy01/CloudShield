
#include "router/http_router.hpp"

namespace cloudshield::router {

cloudshield::http::HttpResponse routeRequest(
    const cloudshield::http::HttpRequest& request
) {
    using cloudshield::http::HttpResponse;

    if (request.method == "GET" && request.target == "/") {
        HttpResponse response;
        response.body = "CloudShield Gateway is running.\n";
        return response;
    }

    if (request.method == "GET" && request.target == "/health") {
        HttpResponse response;
        response.content_type = "application/json";
        response.body = R"({"status":"ok"})";
        return response;
    }

    if (request.target == "/" || request.target == "/health") {
        auto response = cloudshield::http::makeErrorResponse(
            405,
            "Method Not Allowed\n"
        );

        response.reason_phrase = "Method Not Allowed";
        return response;
    }

    auto response = cloudshield::http::makeErrorResponse(
        404,
        "Not Found\n"
    );

    response.reason_phrase = "Not Found";
    return response;
}

}  // namespace cloudshield::router
