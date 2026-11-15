#include <doctest/doctest.h>
#include "data/docker_client.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace status;

static std::string read_fixture(const std::string& name) {
    std::ifstream f(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

// Minimal unix-socket server for tests: accepts one connection, reads the
// request, then either sends `reply` and closes, or stays silent.
struct FakeDockerd {
    std::string path;
    int fd = -1;
    std::thread th;
    std::atomic<bool> got_request{false};
    std::string request;

    FakeDockerd(std::string reply, bool silent) {
        path = "/tmp/fake-dockerd-" + std::to_string(::getpid()) + "-" + std::to_string(rand()) + ".sock";
        ::unlink(path.c_str());
        fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(fd, 1) == 0);
        th = std::thread([this, reply, silent] {
            int c = ::accept(fd, nullptr, nullptr);
            if (c < 0) return;
            char buf[4096];
            ssize_t n = ::read(c, buf, sizeof(buf));
            if (n > 0) { request.assign(buf, size_t(n)); got_request = true; }
            if (silent) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            } else {
                size_t off = 0;
                while (off < reply.size()) {
                    ssize_t w = ::write(c, reply.data() + off, reply.size() - off);
                    if (w <= 0) break;
                    off += size_t(w);
                }
            }
            ::close(c);
        });
    }
    ~FakeDockerd() {
        ::shutdown(fd, SHUT_RDWR);
        if (th.joinable()) th.join();
        ::close(fd);
        ::unlink(path.c_str());
    }
};

TEST_CASE("parse_http_response decodes chunked body") {
    auto r = parse_http_response(read_fixture("http_chunked.txt"));
    CHECK(r.ok);
    CHECK(r.status == 200);
    CHECK(r.body == "[{\"a\":1},{\"b\":2}]\n");
}

TEST_CASE("parse_http_response with Content-Length") {
    auto r = parse_http_response("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n[]");
    CHECK(r.ok);
    CHECK(r.body == "[]");
}

TEST_CASE("parse_http_response non-2xx is an error") {
    auto r = parse_http_response("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n");
    CHECK_FALSE(r.ok);
    CHECK(r.status == 500);
    CHECK(r.error.find("500") != std::string::npos);
}

TEST_CASE("parse_http_response garbage is an error") {
    auto r = parse_http_response("hello");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("parse_containers sorts running first, then by name, strips slash") {
    auto c = parse_containers(read_fixture("containers.json"));
    REQUIRE(c.size() == 4);
    CHECK(c[0].name == "nginx");
    CHECK(c[1].name == "postgres");
    CHECK(c[2].name == "backup");
    CHECK(c[3].name == "jellyfin");
    CHECK(c[0].state == "running");
    CHECK(c[2].status == "Exited (0) 2 hours ago");
}

TEST_CASE("parse_containers on invalid json returns empty") {
    CHECK(parse_containers("{not json").empty());
    CHECK(parse_containers("{\"message\":\"x\"}").empty());
}

TEST_CASE("fetch_docker on missing socket reports an error quickly") {
    auto t0 = std::chrono::steady_clock::now();
    auto d = fetch_docker("/nonexistent/docker.sock");
    auto dt = std::chrono::steady_clock::now() - t0;
    CHECK_FALSE(d.ok);
    CHECK_FALSE(d.error.empty());
    CHECK(dt < std::chrono::milliseconds(2500));
}

TEST_CASE("unix_http_get times out on a silent server") {
    FakeDockerd srv("", true);
    auto t0 = std::chrono::steady_clock::now();
    auto r = unix_http_get(srv.path, "/v1.43/containers/json?all=1", 300);
    auto dt = std::chrono::steady_clock::now() - t0;
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("timeout") != std::string::npos);
    CHECK(dt < std::chrono::milliseconds(1000));
}

TEST_CASE("unix_http_get reads a chunked reply and sends a proper GET") {
    FakeDockerd srv(read_fixture("http_chunked.txt"), false);
    auto r = unix_http_get(srv.path, "/v1.43/containers/json?all=1", 2000);
    CHECK(r.ok);
    CHECK(r.body == "[{\"a\":1},{\"b\":2}]\n");
    CHECK(srv.request.rfind("GET /v1.43/containers/json?all=1 HTTP/1.1\r\n", 0) == 0);
}

TEST_CASE("fetch_docker end to end") {
    std::string body = read_fixture("containers.json");
    std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    FakeDockerd srv(reply, false);
    auto d = fetch_docker(srv.path);
    CHECK(d.ok);
    CHECK(d.containers.size() == 4);
    // Unversioned path: Docker 29+ rejects API versions below 1.44.
    CHECK(srv.request.rfind("GET /containers/json?all=1 HTTP/1.1\r\n", 0) == 0);
}
