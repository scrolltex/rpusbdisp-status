#pragma once

#include "data/snapshot.h"

#include <string>
#include <vector>

namespace status {

struct HttpResult {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;
};

// Parses a full HTTP/1.1 response (Content-Length, chunked, or read-to-EOF).
HttpResult parse_http_response(const std::string& raw);

// GET `target` over a unix socket. `timeout_ms` bounds the whole exchange.
HttpResult unix_http_get(const std::string& socket_path, const std::string& target, int timeout_ms);

// Names[0] without leading '/', State, Status; running first, then by name.
std::vector<ContainerInfo> parse_containers(const std::string& json);

// Read-only: GET /containers/json?all=1, timeout 2000 ms.
DockerSnapshot fetch_docker(const std::string& socket_path);

}  // namespace status
