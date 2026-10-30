/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cctype>
#include <csignal>
#include <format>
#include <list>
#include <memory>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <IO.h>
#include <JSON.h>
#include <Logging.h>
#include <StringUtil.h>

#include <OAuth.h>

// The environment, for the programs run_and_wait() runs: POSIX has no
// header that declares it.
extern char **environ;

namespace ST {

namespace {

using Clock = std::chrono::steady_clock;

// How long a connection may stay open without sending its request. A
// browser may open one before it needs it, and use it a little later.
constexpr std::chrono::seconds ConnectionLifetime { 30 };

// The most connections handled at a time. Past that, new ones wait.
constexpr size_t MaxConnections = 16;

// How often the wait checks its stop token.
constexpr std::chrono::milliseconds PollInterval { 200 };

// How long sending a response may take. It's small, and fits in the
// socket's buffer right away.
constexpr std::chrono::seconds SendTimeout { 1 };

// The most of a request that's read: the request line is all that's needed.
constexpr size_t MaxRequestSize = 16 * 1024;

LibCError openssl_error(std::string_view what)
{
    char buffer[256] { };
    ERR_error_string_n(ERR_get_error(), buffer, sizeof(buffer));
    return LibCError { std::format("{}: {}", what, buffer) };
}

int hex_value(char c)
{
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return c - '0';
    }
    auto const lower = std::tolower(static_cast<unsigned char>(c));
    return (lower >= 'a' && lower <= 'f') ? lower - 'a' + 10 : -1;
}

// %XX is the byte XX, and in a query string, + is a space.
std::string url_decode(std::string_view text)
{
    std::string decoded;
    for (size_t ix = 0; ix < text.size(); ++ix) {
        if (text[ix] == '+') {
            decoded += ' ';
        } else if (text[ix] == '%' && ix + 2 < text.size() && hex_value(text[ix + 1]) >= 0 && hex_value(text[ix + 2]) >= 0) {
            decoded += static_cast<char>(hex_value(text[ix + 1]) * 16 + hex_value(text[ix + 2]));
            ix += 2;
        } else {
            decoded += text[ix];
        }
    }
    return decoded;
}

void close_socket(int fd)
{
    if (fd >= 0) {
        ::close(fd);
    }
}

// A socket listening on `address`, of `size` bytes, non-blocking so that
// accepting a connection that's gone again doesn't block the wait.
std::expected<int, LibCError> listen_on(sockaddr const *address, socklen_t size)
{
    int const fd = ::socket(address->sa_family, SOCK_STREAM, 0);
    if (fd < 0) {
        return std::unexpected(LibCError { });
    }
    int on = 1;
    // A login right after the last one finds the port in TIME_WAIT.
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (address->sa_family == AF_INET6) {
        ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
    }
    if (::bind(fd, address, size) < 0 || ::listen(fd, 4) < 0) {
        LibCError const error { };
        ::close(fd);
        return std::unexpected(error);
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    // Not for the browser that open_browser() starts to inherit.
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

// The browser hangs up whenever it likes. Writing to a connection it closed
// would raise SIGPIPE, which ends the program.
void ignore_sigpipe([[maybe_unused]] int fd)
{
#ifdef SO_NOSIGPIPE
    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
}

std::string page(int status, std::string_view reason, std::string_view text)
{
    auto const body = std::format(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>SweatTrails</title></head>"
        "<body style=\"font-family: sans-serif\"><p>{}</p></body></html>\r\n",
        text);
    return std::format(
        "HTTP/1.1 {} {}\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: {}\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{}",
        status, reason, body.size(), body);
}

using Ssl = std::unique_ptr<SSL, decltype(&SSL_free)>;

// A connection from the browser. It's handled without blocking: its TLS
// handshake and request arrive bit by bit, while the wait looks after the
// other connections, the deadline and the stop token.
struct Connection {
    int               fd { -1 };
    Ssl               ssl { nullptr, SSL_free };
    Clock::time_point expires { };
    bool              handshaken { false };
    std::string       request { };
    short             events { POLLIN }; // What OpenSSL waits for

    Connection(int fd, SSL_CTX *context)
        : fd(fd)
        , ssl(SSL_new(context), SSL_free)
        , expires(Clock::now() + ConnectionLifetime)
    {
        // Accepted from a non-blocking socket, which on macOS makes this one
        // non-blocking too, but not on Linux.
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        ignore_sigpipe(fd);
        if (ssl) {
            SSL_set_fd(ssl.get(), fd);
            SSL_set_accept_state(ssl.get());
        }
    }

    Connection(Connection const &) = delete;
    Connection &operator=(Connection const &) = delete;

    ~Connection()
    {
        ssl.reset();
        ::close(fd);
    }
};

enum class Progress {
    Waiting,  // For more from the browser
    Complete, // The request's head is in
    Failed,   // Such as a browser that doesn't trust the certificate
};

// What OpenSSL needs before `result`, a call that didn't finish, can go on.
Progress waiting_for(Connection &connection, int result)
{
    switch (SSL_get_error(connection.ssl.get(), result)) {
    case SSL_ERROR_WANT_READ:
        connection.events = POLLIN;
        return Progress::Waiting;
    case SSL_ERROR_WANT_WRITE:
        connection.events = POLLOUT;
        return Progress::Waiting;
    default:
        trace(OAuth, "Dropping a connection: {}", openssl_error("TLS").to_string());
        return Progress::Failed;
    }
}

// Takes what has arrived: the rest of the handshake, and then the request.
Progress advance(Connection &connection)
{
    if (!connection.ssl) {
        return Progress::Failed;
    }
    if (!connection.handshaken) {
        if (auto const result = SSL_accept(connection.ssl.get()); result != 1) {
            return waiting_for(connection, result);
        }
        connection.handshaken = true;
    }
    char buffer[4096];
    while (true) {
        auto const count = SSL_read(connection.ssl.get(), buffer, sizeof(buffer));
        if (count <= 0) {
            return waiting_for(connection, count);
        }
        connection.request.append(buffer, static_cast<size_t>(count));
        if (connection.request.contains("\r\n\r\n")) {
            return Progress::Complete;
        }
        if (connection.request.size() > MaxRequestSize) {
            return Progress::Failed;
        }
    }
}

// Sends `response`, and gives up after SendTimeout.
void respond(Connection &connection, std::string_view response)
{
    auto const deadline = Clock::now() + SendTimeout;
    while (Clock::now() < deadline) {
        auto const result = SSL_write(connection.ssl.get(), response.data(), static_cast<int>(response.size()));
        if (result > 0) {
            SSL_shutdown(connection.ssl.get());
            return;
        }
        if (waiting_for(connection, result) == Progress::Failed) {
            return;
        }
        pollfd fd { .fd = connection.fd, .events = connection.events, .revents = 0 };
        ::poll(&fd, 1, static_cast<int>(PollInterval.count()));
    }
}

// The path and query parameters of a request's head: GET
// /callback?code=...&state=... HTTP/1.1. Nothing if it isn't a GET.
std::optional<std::pair<std::string, QueryParameters>> parse_request(std::string_view head)
{
    auto const parts = split(head.substr(0, head.find("\r\n")), ' ');
    if (parts.size() != 3 || parts[0] != "GET") {
        return { };
    }
    auto const target = std::string_view { parts[1] };
    auto const query_start = std::min(target.find('?'), target.size());
    return std::pair {
        std::string { target.substr(0, query_start) },
        parse_query(target.substr(std::min(query_start + 1, target.size()))),
    };
}

// Answers the request `connection` sent. Returns its query parameters if
// it's the redirect the login waits for: a request for `path`, with the
// login's `state`.
std::optional<QueryParameters> answer(Connection &connection, std::string_view path, std::string_view state)
{
    auto const request = parse_request(connection.request);
    // Not the query, which has the authorization code.
    trace(OAuth, "Request for `{}`", request ? request->first : std::string { "?" });
    if (!request || request->first != path) {
        respond(connection, page(404, "Not Found", "Not found."));
        return { };
    }
    auto const &parameters = request->second;
    // From an earlier login, such as a tab reloaded: this login goes on.
    if (!parameters.contains("state") || parameters.at("state") != state) {
        respond(connection, page(400, "Bad Request", "This login is over. You can close this window."));
        return { };
    }
    respond(connection, page(200, "OK", parameters.contains("error") ? "The login didn't go through. You can close this window, and try again from SweatTrails." : "You're logged in. You can close this window and go back to SweatTrails."));
    return parameters;
}

}

Decoded<OAuthTokens> OAuthTokens::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError { JSONError::Code::TypeMismatch, "the tokens aren't an object" });
    }
    return OAuthTokens {
        .access_token = TRY_EVAL(json.try_get<std::string>("access_token")),
        .refresh_token = TRY_EVAL(json.try_get<std::string>("refresh_token")),
        .expires_at = TRY_EVAL(json.try_get<i64>("expires_at")),
        .code_verifier = json.try_get<std::string>("code_verifier").transform([](std::string v) { return std::optional { std::move(v) }; }).value_or(std::nullopt),
    };
}

JSONValue OAuthTokens::encode() const
{
    auto json = JSONValue::object();
    json["access_token"] = JSONValue { access_token };
    json["refresh_token"] = JSONValue { refresh_token };
    json["expires_at"] = JSONValue { expires_at };
    if (code_verifier) {
        json["code_verifier"] = JSONValue { *code_verifier };
    }
    return json;
}

std::optional<OAuthTokens> read_tokens(fs::path const &file)
{
    std::error_code ec { };
    if (!fs::exists(file, ec)) {
        return { };
    }
    auto const contents = read_file_by_name(file.string());
    if (!contents) {
        warning(OAuth, "Could not read `{}`: {}", file.string(), contents.error().to_string());
        return { };
    }
    auto const json = JSONValue::deserialize(*contents);
    if (!json) {
        warning(OAuth, "Ignoring `{}`: {}", file.string(), json.error().to_string());
        return { };
    }
    auto tokens = OAuthTokens::decode(*json);
    if (!tokens) {
        warning(OAuth, "Ignoring `{}`: {}", file.string(), tokens.error().to_string());
        return { };
    }
    return std::move(*tokens);
}

std::expected<void, LibCError> write_tokens(fs::path const &file, OAuthTokens const &tokens)
{
    // On Linux, nothing else may have made SweatTrails' config folder yet.
    std::error_code ec { };
    fs::create_directories(file.parent_path(), ec);
    return write_file_atomically(file, tokens.encode().serialize(), fs::perms::owner_read | fs::perms::owner_write);
}

std::string random_token(size_t bytes)
{
    std::string buffer(bytes, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char *>(buffer.data()), static_cast<int>(bytes)) != 1) {
        // OpenSSL's generator only fails if the system's has nothing to give.
        fatal("Could not get random bytes: {}", openssl_error("RAND_bytes").to_string());
    }
    return base64url(buffer);
}

Pkce Pkce::make()
{
    // 32 bytes make 43 characters, the least RFC 7636 allows.
    auto verifier = random_token(32);
    auto challenge = challenge_for(verifier);
    return Pkce { .verifier = std::move(verifier), .challenge = std::move(challenge) };
}

std::string Pkce::challenge_for(std::string_view verifier)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  size { 0 };
    EVP_Digest(verifier.data(), verifier.size(), digest, &size, EVP_sha256(), nullptr);
    return base64url(std::string_view { reinterpret_cast<char const *>(digest), size });
}

QueryParameters parse_query(std::string_view query)
{
    QueryParameters parameters;
    for (auto const &pair : split(query, '&')) {
        if (pair.empty()) {
            continue;
        }
        auto const equals = std::min(pair.find('='), pair.size());
        parameters[url_decode(pair.substr(0, equals))] = url_decode(std::string_view { pair }.substr(std::min(equals + 1, pair.size())));
    }
    return parameters;
}

std::expected<TlsContext, LibCError> TlsContext::load(fs::path const &certificate, fs::path const &key)
{
    std::shared_ptr<SSL_CTX> context { SSL_CTX_new(TLS_server_method()), SSL_CTX_free };
    if (!context) {
        return std::unexpected(openssl_error("Could not set up TLS"));
    }
    SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_chain_file(context.get(), certificate.c_str()) != 1) {
        return std::unexpected(openssl_error(std::format("Could not read the certificate `{}`", certificate.string())));
    }
    if (SSL_CTX_use_PrivateKey_file(context.get(), key.c_str(), SSL_FILETYPE_PEM) != 1) {
        return std::unexpected(openssl_error(std::format("Could not read the key `{}`", key.string())));
    }
    if (SSL_CTX_check_private_key(context.get()) != 1) {
        return std::unexpected(openssl_error(std::format("The key `{}` isn't the certificate's", key.string())));
    }
    return TlsContext { .context = std::move(context) };
}

LoopbackServer::LoopbackServer(LoopbackServer &&other) noexcept
    : m_tls(std::move(other.m_tls))
    , m_ipv4(std::exchange(other.m_ipv4, -1))
    , m_ipv6(std::exchange(other.m_ipv6, -1))
    , m_port(other.m_port)
{
}

LoopbackServer::~LoopbackServer()
{
    close_socket(m_ipv4);
    close_socket(m_ipv6);
}

std::expected<LoopbackServer, LibCError> LoopbackServer::listen(int port, TlsContext tls)
{
    LoopbackServer server;
    server.m_tls = std::move(tls);

    sockaddr_in ipv4 { };
    ipv4.sin_family = AF_INET;
    ipv4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ipv4.sin_port = htons(static_cast<uint16_t>(port));
    auto const fd4 = listen_on(reinterpret_cast<sockaddr const *>(&ipv4), sizeof(ipv4));
    if (!fd4) {
        return std::unexpected(LibCError { std::format("Could not listen on port {}: {}", port, fd4.error().to_string()) });
    }
    server.m_ipv4 = *fd4;
    // With port 0, the system picked one: IPv6 has to listen on the same.
    socklen_t size = sizeof(ipv4);
    ::getsockname(server.m_ipv4, reinterpret_cast<sockaddr *>(&ipv4), &size);
    server.m_port = ntohs(ipv4.sin_port);

    sockaddr_in6 ipv6 { };
    ipv6.sin6_family = AF_INET6;
    ipv6.sin6_addr = in6addr_loopback;
    ipv6.sin6_port = htons(static_cast<uint16_t>(server.m_port));
    auto const fd6 = listen_on(reinterpret_cast<sockaddr const *>(&ipv6), sizeof(ipv6));
    if (!fd6 && fd6.error().err_no == EADDRINUSE) {
        return std::unexpected(LibCError { std::format("Could not listen on port {}: {}", server.m_port, fd6.error().to_string()) });
    }
    // Without IPv6, the browser can only take localhost to be 127.0.0.1.
    if (!fd6) {
        trace(OAuth, "Not listening on IPv6: {}", fd6.error().to_string());
    }
    server.m_ipv6 = fd6.value_or(-1);
    return server;
}

std::expected<QueryParameters, RedirectError> LoopbackServer::wait_for_redirect(std::string_view path, std::string_view state, std::chrono::seconds timeout, std::stop_token const &stop)
{
    auto const            deadline = Clock::now() + timeout;
    std::list<Connection> connections;
    while (true) {
        if (stop.stop_requested()) {
            return std::unexpected(RedirectError { .kind = RedirectError::Kind::Stopped, .message = "The login was stopped" });
        }
        auto const now = Clock::now();
        if (now >= deadline) {
            return std::unexpected(RedirectError { .kind = RedirectError::Kind::TimedOut, .message = "The login timed out" });
        }
        connections.remove_if([now](Connection const &connection) { return connection.expires <= now; });

        // The listening sockets first, then a pollfd per connection, in
        // the list's order.
        std::vector<pollfd> fds;
        size_t              listeners { 0 };
        if (connections.size() < MaxConnections) {
            for (auto const fd : { m_ipv4, m_ipv6 }) {
                if (fd >= 0) {
                    fds.push_back(pollfd { .fd = fd, .events = POLLIN, .revents = 0 });
                    ++listeners;
                }
            }
        }
        for (auto const &connection : connections) {
            fds.push_back(pollfd { .fd = connection.fd, .events = connection.events, .revents = 0 });
        }
        auto const left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if (::poll(fds.data(), fds.size(), static_cast<int>(std::min(left, PollInterval).count())) < 0 && errno != EINTR) {
            return std::unexpected(RedirectError { .message = std::format("The login failed: {}", LibCError { }.to_string()) });
        }

        auto connection = connections.begin();
        for (size_t ix = listeners; ix < fds.size(); ++ix) {
            if (fds[ix].revents == 0) {
                ++connection;
                continue;
            }
            auto const progress = advance(*connection);
            if (progress == Progress::Waiting) {
                ++connection;
                continue;
            }
            auto parameters = (progress == Progress::Complete)
                ? answer(*connection, path, state)
                : std::nullopt;
            connection = connections.erase(connection);
            if (parameters) {
                return std::move(*parameters);
            }
        }
        // New connections after the others: the list's order has to match
        // the pollfds' above.
        for (size_t ix = 0; ix < listeners; ++ix) {
            if ((fds[ix].revents & POLLIN) == 0) {
                continue;
            }
            if (int const fd = ::accept(fds[ix].fd, nullptr, nullptr); fd >= 0) {
                connections.emplace_back(fd, m_tls.context.get());
            }
        }
    }
}

std::expected<int, LibCError> run_and_wait(std::vector<std::string> const &command)
{
    // Not Process: its pipes' reader threads outlive it.
    std::vector<char *> argv;
    for (auto const &argument : command) {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);
    pid_t pid { 0 };
    if (auto const error = ::posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ); error != 0) {
        return std::unexpected(LibCError { error });
    }
    int status { 0 };
    while (::waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            return std::unexpected(LibCError { });
        }
    }
    if (!WIFEXITED(status)) {
        return std::unexpected(LibCError { std::format("`{}` ended with signal {}", command.front(), WTERMSIG(status)) });
    }
    return WEXITSTATUS(status);
}

std::expected<void, LibCError> open_browser(std::string const &url)
{
#ifdef __APPLE__
    auto const exit_code = run_and_wait({ "open", url });
#else
    auto const exit_code = run_and_wait({ "xdg-open", url });
#endif
    if (!exit_code) {
        return std::unexpected(exit_code.error());
    }
    if (*exit_code != 0) {
        return std::unexpected(LibCError { std::format("exit code {}", *exit_code) });
    }
    return { };
}

std::expected<std::string, std::string> login_code(std::expected<QueryParameters, RedirectError> const &redirect, std::string_view service)
{
    if (!redirect) {
        return std::unexpected(redirect.error().kind == RedirectError::Kind::TimedOut
                ? std::format("The {} login timed out. Try again from SweatTrails when you're ready to log in.", service)
                : redirect.error().to_string());
    }
    // access_denied when the user doesn't allow SweatTrails access.
    if (redirect->contains("error")) {
        return std::unexpected(std::format("The {} login didn't go through: {}", service, redirect->at("error")));
    }
    if (!redirect->contains("code") || redirect->at("code").empty()) {
        return std::unexpected(std::format("The {} login came back without a code. Try again.", service));
    }
    return redirect->at("code");
}

}
