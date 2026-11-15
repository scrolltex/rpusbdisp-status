#include "data/docker_client.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace status {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool decode_chunked(const std::string& in, std::string& out) {
    size_t pos = 0;
    out.clear();
    while (true) {
        size_t eol = in.find("\r\n", pos);
        if (eol == std::string::npos) return false;
        size_t len = 0;
        try {
            len = std::stoul(in.substr(pos, eol - pos), nullptr, 16);
        } catch (...) {
            return false;
        }
        pos = eol + 2;
        if (len == 0) return true;
        if (pos + len > in.size()) return false;
        out.append(in, pos, len);
        pos += len + 2;  // skip chunk data and its CRLF
    }
}

struct Fd {
    int fd = -1;
    ~Fd() { if (fd >= 0) ::close(fd); }
};

}  // namespace

HttpResult parse_http_response(const std::string& raw) {
    HttpResult r;
    size_t hdr_end = raw.find("\r\n\r\n");
    if (raw.rfind("HTTP/", 0) != 0 || hdr_end == std::string::npos) {
        r.error = "malformed HTTP response";
        return r;
    }
    size_t sp = raw.find(' ');
    try {
        r.status = std::stoi(raw.substr(sp + 1, 3));
    } catch (...) {
        r.error = "malformed HTTP status line";
        return r;
    }

    bool chunked = false;
    long content_length = -1;
    size_t line_start = raw.find("\r\n") + 2;
    while (line_start < hdr_end) {
        size_t line_end = raw.find("\r\n", line_start);
        std::string line = raw.substr(line_start, line_end - line_start);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = lower(line.substr(0, colon));
            std::string val = line.substr(colon + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            if (key == "transfer-encoding" && lower(val).find("chunked") != std::string::npos) chunked = true;
            if (key == "content-length") content_length = std::atol(val.c_str());
        }
        line_start = line_end + 2;
    }

    std::string body = raw.substr(hdr_end + 4);
    if (chunked) {
        if (!decode_chunked(body, r.body)) {
            r.error = "truncated chunked body";
            return r;
        }
    } else if (content_length >= 0) {
        if (body.size() < size_t(content_length)) {
            r.error = "truncated body";
            return r;
        }
        r.body = body.substr(0, size_t(content_length));
    } else {
        r.body = body;
    }

    if (r.status < 200 || r.status >= 300) {
        r.error = "HTTP " + std::to_string(r.status);
        return r;
    }
    r.ok = true;
    return r;
}

HttpResult unix_http_get(const std::string& socket_path, const std::string& target, int timeout_ms) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    auto remaining_ms = [&] {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
        return int(std::max<long long>(0, left));
    };
    HttpResult r;

    sockaddr_un addr{};
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        r.error = "socket path too long";
        return r;
    }
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);

    Fd s;
    s.fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (s.fd < 0) {
        r.error = std::string("socket: ") + std::strerror(errno);
        return r;
    }
    if (::connect(s.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        if (errno != EINPROGRESS && errno != EAGAIN) {
            r.error = std::string("connect: ") + std::strerror(errno);
            return r;
        }
        pollfd p{s.fd, POLLOUT, 0};
        if (::poll(&p, 1, remaining_ms()) <= 0) {
            r.error = "connect timeout";
            return r;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        ::getsockopt(s.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            r.error = std::string("connect: ") + std::strerror(err);
            return r;
        }
    }

    const std::string req = "GET " + target + " HTTP/1.1\r\nHost: docker\r\nConnection: close\r\n\r\n";
    size_t sent = 0;
    while (sent < req.size()) {
        pollfd p{s.fd, POLLOUT, 0};
        if (::poll(&p, 1, remaining_ms()) <= 0) {
            r.error = "write timeout";
            return r;
        }
        ssize_t n = ::send(s.fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno != EAGAIN && errno != EINTR) {
            r.error = std::string("write: ") + std::strerror(errno);
            return r;
        }
        if (n > 0) sent += size_t(n);
    }

    std::string raw;
    char buf[8192];
    while (true) {
        int left = remaining_ms();
        if (left == 0) {
            r.error = "read timeout";
            return r;
        }
        pollfd p{s.fd, POLLIN, 0};
        int pr = ::poll(&p, 1, left);
        if (pr == 0) {
            r.error = "read timeout";
            return r;
        }
        if (pr < 0) {
            if (errno == EINTR) continue;
            r.error = std::string("poll: ") + std::strerror(errno);
            return r;
        }
        ssize_t n = ::read(s.fd, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            r.error = std::string("read: ") + std::strerror(errno);
            return r;
        }
        raw.append(buf, size_t(n));
        if (raw.size() > 8 * 1024 * 1024) {
            r.error = "response too large";
            return r;
        }
    }
    return parse_http_response(raw);
}

std::vector<ContainerInfo> parse_containers(const std::string& json) {
    std::vector<ContainerInfo> out;
    auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_array()) return out;
    for (const auto& c : j) {
        if (!c.is_object()) continue;
        ContainerInfo ci;
        if (c.contains("Names") && c["Names"].is_array() && !c["Names"].empty() && c["Names"][0].is_string()) {
            ci.name = c["Names"][0].get<std::string>();
            if (!ci.name.empty() && ci.name[0] == '/') ci.name.erase(0, 1);
        } else if (c.contains("Id") && c["Id"].is_string()) {
            ci.name = c["Id"].get<std::string>().substr(0, 12);
        }
        if (c.contains("State") && c["State"].is_string()) ci.state = c["State"].get<std::string>();
        if (c.contains("Status") && c["Status"].is_string()) ci.status = c["Status"].get<std::string>();
        out.push_back(std::move(ci));
    }
    std::sort(out.begin(), out.end(), [](const ContainerInfo& a, const ContainerInfo& b) {
        bool ra = a.state == "running", rb = b.state == "running";
        if (ra != rb) return ra;
        return a.name < b.name;
    });
    return out;
}

DockerSnapshot fetch_docker(const std::string& socket_path) {
    DockerSnapshot d;
    auto r = unix_http_get(socket_path, "/containers/json?all=1", 2000);
    if (!r.ok) {
        d.error = r.error;
        return d;
    }
    auto j = nlohmann::json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        d.error = "unexpected response";
        return d;
    }
    d.containers = parse_containers(r.body);
    d.ok = true;
    return d;
}

}  // namespace status
