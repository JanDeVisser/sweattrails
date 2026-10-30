/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * The parts of an OAuth 2 login that don't depend on the service: PKCE,
 * opening the browser, and a small HTTPS server on localhost that catches
 * the browser when the service sends it back with the authorization code.
 * See specs/wahoo-download.md.
 */

#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <Error.h>
#include <JSON.h>

struct ssl_ctx_st;

namespace ST {

namespace fs = std::filesystem;

// The proof that the app that trades the authorization code for tokens is
// the one that asked for it (RFC 7636): the login sends the challenge, the
// token request the verifier.
struct Pkce {
    std::string verifier { };
    std::string challenge { }; // S256: base64url(sha256(verifier))

    static Pkce        make();
    static std::string challenge_for(std::string_view verifier);
};

// What a login gives: Wahoo's and Strava's are kept the same way. A Wahoo
// refresh sends the PKCE verifier again, so it's kept with the tokens;
// Strava has none.
struct OAuthTokens {
    std::string                access_token { };
    std::string                refresh_token { };
    i64                        expires_at { 0 }; // Unix time
    std::optional<std::string> code_verifier { };

    static Decoded<OAuthTokens> decode(JSONValue const &json);
    [[nodiscard]] JSONValue     encode() const;
};

// The tokens in `file`, or nothing if there's no file or it can't be read.
std::optional<OAuthTokens> read_tokens(fs::path const &file);

// Writes `tokens` to `file`, readable only by the user.
std::expected<void, LibCError> write_tokens(fs::path const &file, OAuthTokens const &tokens);

// `bytes` random bytes, base64url-encoded: for PKCE's verifier, and the
// login's `state`.
std::string random_token(size_t bytes);

using QueryParameters = std::map<std::string, std::string>;

// The parameters in a query string, `a=1&b=2`, percent-decoded.
QueryParameters parse_query(std::string_view query);

// A certificate and its key, for serving HTTPS.
struct TlsContext {
    std::shared_ptr<ssl_ctx_st> context { };

    static std::expected<TlsContext, LibCError> load(fs::path const &certificate, fs::path const &key);
};

struct RedirectError {
    enum class Kind {
        TimedOut,
        Stopped, // The stop token asked the wait to stop
        Failed,
    };

    Kind        kind { Kind::Failed };
    std::string message { };

    [[nodiscard]] std::string to_string() const { return message; }
};

// Listens on 127.0.0.1 and ::1, for the one request a login ends with. It
// binds when it's made, so that a port that's taken is an error before the
// browser opens.
struct LoopbackServer {
    LoopbackServer(LoopbackServer &&other) noexcept;
    LoopbackServer &operator=(LoopbackServer &&) = delete;
    LoopbackServer(LoopbackServer const &) = delete;
    ~LoopbackServer();

    // Port 0 takes any free port, as tests do.
    static std::expected<LoopbackServer, LibCError> listen(int port, TlsContext tls);

    [[nodiscard]] int port() const { return m_port; }

    // Waits for a request for `path` with the login's `state`, answers it
    // with a page that says the window can be closed, and returns its query
    // parameters. The wait goes on past other requests: one for
    // /favicon.ico gets a 404, and one from an earlier login, with another
    // state, a page that says that login is over. Connections that fail,
    // such as from a browser that doesn't trust the certificate, are
    // dropped. Connections are handled side by side, so that one that's
    // slow, or that the browser opens before it needs it, holds up neither
    // the others nor the timeout and the stop token.
    std::expected<QueryParameters, RedirectError> wait_for_redirect(std::string_view path, std::string_view state, std::chrono::seconds timeout, std::stop_token const &stop);

private:
    LoopbackServer() = default;

    TlsContext m_tls { };
    int        m_ipv4 { -1 };
    int        m_ipv6 { -1 };
    int        m_port { 0 };
};

// Runs `command`, the program and its arguments, looking for the program
// on the PATH, and waits for it to end. Returns its exit code.
std::expected<int, LibCError> run_and_wait(std::vector<std::string> const &command);

// Opens `url` in the user's browser.
std::expected<void, LibCError> open_browser(std::string const &url);

// The code the browser brought back from `service`'s login, such as
// "Wahoo", or what to tell the user if it didn't bring one.
std::expected<std::string, std::string> login_code(std::expected<QueryParameters, RedirectError> const &redirect, std::string_view service);

}
