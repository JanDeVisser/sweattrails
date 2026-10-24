/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Tests PKCE and the login's HTTPS server, with a certificate made for the
 * test and a TLS client in the test. Nothing here leaves the machine.
 */

#include <algorithm>
#include <chrono>
#include <csignal>
#include <stop_token>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sys/socket.h>
#include <unistd.h>

#include <OAuth.h>

#include <TempDir.h>
#include <TestApp.h>

using namespace ST;
using namespace ST::Test;
using namespace std::chrono_literals;

namespace {

// A self-signed certificate for localhost, valid for an hour.
struct TestCertificate {
    TempDir  dir { "oauth-certificate" };
    fs::path certificate { dir.path / "localhost.pem" };
    fs::path key { dir.path / "localhost-key.pem" };

    TestCertificate()
    {
        auto *pkey = EVP_EC_gen("P-256");
        auto *x509 = X509_new();
        X509_set_version(x509, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
        X509_gmtime_adj(X509_getm_notBefore(x509), 0);
        X509_gmtime_adj(X509_getm_notAfter(x509), 60 * 60);
        X509_set_pubkey(x509, pkey);
        auto *name = X509_NAME_new();
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<unsigned char const *>("localhost"), -1, -1, 0);
        X509_set_subject_name(x509, name);
        X509_set_issuer_name(x509, name);
        X509_NAME_free(name);
        X509_sign(x509, pkey, EVP_sha256());

        auto *certificate_file = BIO_new_file(certificate.c_str(), "w");
        PEM_write_bio_X509(certificate_file, x509);
        BIO_free(certificate_file);
        auto *key_file = BIO_new_file(key.c_str(), "w");
        PEM_write_bio_PrivateKey(key_file, pkey, nullptr, nullptr, 0, nullptr, nullptr);
        BIO_free(key_file);
        X509_free(x509);
        EVP_PKEY_free(pkey);
    }

    [[nodiscard]] LoopbackServer server(int port = 0) const
    {
        auto server = LoopbackServer::listen(port, MUST_EVAL(TlsContext::load(certificate, key)));
        if (!server) {
            fatal("Could not listen: {}", server.error().to_string());
        }
        return std::move(*server);
    }
};

int connect_to(int port, bool ipv6 = false)
{
    int const fd = ::socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (ipv6) {
        sockaddr_in6 address { };
        address.sin6_family = AF_INET6;
        address.sin6_addr = in6addr_loopback;
        address.sin6_port = htons(static_cast<uint16_t>(port));
        ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    } else {
        sockaddr_in address { };
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<uint16_t>(port));
        ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    }
    return fd;
}

// A browser, as far as the server can tell: asks for `target` over TLS,
// without checking the certificate, and returns the response.
std::string https_get(int port, std::string_view target, bool ipv6 = false)
{
    auto *context = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);
    auto     *ssl = SSL_new(context);
    int const fd = connect_to(port, ipv6);
    SSL_set_fd(ssl, fd);
    std::string response;
    if (SSL_connect(ssl) == 1) {
        auto const request = std::format("GET {} HTTP/1.1\r\nHost: localhost:{}\r\n\r\n", target, port);
        SSL_write(ssl, request.data(), static_cast<int>(request.size()));
        char buffer[4096];
        for (int count; (count = SSL_read(ssl, buffer, sizeof(buffer))) > 0;) {
            response.append(buffer, static_cast<size_t>(count));
        }
    }
    SSL_free(ssl);
    SSL_CTX_free(context);
    ::close(fd);
    return response;
}

// Sends `text` without TLS, as something that isn't a browser might.
void plain_send(int port, std::string_view text)
{
    int const fd = connect_to(port);
    ::write(fd, text.data(), text.size());
    ::close(fd);
}

void test_challenge()
{
    // RFC 7636, appendix B.
    CHECK_EQ(Pkce::challenge_for("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

void test_verifier()
{
    auto const pkce = Pkce::make();
    CHECK_EQ(pkce.verifier.size(), 43uz);
    CHECK(std::ranges::all_of(pkce.verifier, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; }));
    CHECK_EQ(pkce.challenge, Pkce::challenge_for(pkce.verifier));
    CHECK(Pkce::make().verifier != pkce.verifier);
}

void test_parse_query()
{
    auto const parameters = parse_query("code=a%20b&state=x+y&empty=&flag&&bad=%zz");
    CHECK_EQ(parameters.size(), 5uz);
    CHECK_EQ(parameters.at("code"), "a b");
    CHECK_EQ(parameters.at("state"), "x y");
    CHECK_EQ(parameters.at("empty"), "");
    CHECK_EQ(parameters.at("flag"), "");
    CHECK_EQ(parameters.at("bad"), "%zz");
    CHECK(parse_query("").empty());
}

void test_redirect()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    std::string           response;
    std::jthread          browser { [&] { response = https_get(server.port(), "/callback?code=the%20code&state=s"); } };
    auto const            parameters = server.wait_for_redirect("/callback", "s", 5s, { });
    browser.join();
    CHECK(parameters.has_value());
    if (parameters) {
        CHECK_EQ(parameters->at("code"), "the code");
        CHECK_EQ(parameters->at("state"), "s");
    }
    CHECK(response.starts_with("HTTP/1.1 200 OK"));
    CHECK(response.contains("You're logged in"));
}

void test_redirect_over_ipv6()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    std::jthread          browser { [&] { https_get(server.port(), "/callback?code=c&state=s", true); } };
    auto const            parameters = server.wait_for_redirect("/callback", "s", 5s, { });
    browser.join();
    CHECK(parameters.has_value() && parameters->at("code") == "c");
}

void test_denied_login()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    std::string           response;
    std::jthread          browser { [&] { response = https_get(server.port(), "/callback?error=access_denied&state=s"); } };
    auto const            parameters = server.wait_for_redirect("/callback", "s", 5s, { });
    browser.join();
    CHECK(parameters.has_value() && parameters->at("error") == "access_denied");
    CHECK(response.contains("didn't go through"));
}

void test_other_requests_are_not_the_redirect()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    std::string           favicon;
    std::jthread          browser { [&] {
        favicon = https_get(server.port(), "/favicon.ico");
        https_get(server.port(), "/callbackx?code=wrong&state=s");
        https_get(server.port(), "/callback?code=right&state=s");
    } };
    auto const parameters = server.wait_for_redirect("/callback", "s", 5s, { });
    browser.join();
    CHECK(favicon.starts_with("HTTP/1.1 404"));
    CHECK(parameters.has_value() && parameters->at("code") == "right");
}

void test_failed_connections_are_dropped()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    std::jthread          browser { [&] {
        // Not TLS, and a connection that hangs up before the handshake.
        plain_send(server.port(), "GET /callback?code=plain&state=s HTTP/1.1\r\n\r\n");
        plain_send(server.port(), "");
        https_get(server.port(), "/callback?code=tls&state=s");
    } };
    auto const parameters = server.wait_for_redirect("/callback", "s", 5s, { });
    browser.join();
    CHECK(parameters.has_value() && parameters->at("code") == "tls");
}

// A browser may open a connection it doesn't use yet, as Chrome's
// preconnect does: the login goes on alongside it.
// A callback from an earlier login, such as a tab reloaded, has another
// state: it's told that login is over, and this one goes on.
void test_earlier_login_is_not_the_redirect()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    std::string           stale;
    std::string           without_state;
    std::jthread          browser { [&] {
        stale = https_get(server.port(), "/callback?code=old&state=old");
        without_state = https_get(server.port(), "/callback?code=none");
        https_get(server.port(), "/callback?code=new&state=s");
    } };
    auto const parameters = server.wait_for_redirect("/callback", "s", 5s, { });
    browser.join();
    CHECK(stale.starts_with("HTTP/1.1 400"));
    CHECK(stale.contains("This login is over"));
    CHECK(!stale.contains("You're logged in"));
    CHECK(without_state.starts_with("HTTP/1.1 400"));
    CHECK(parameters.has_value() && parameters->at("code") == "new");
}

// The browser keeps its connection open after the request: the login
// doesn't wait for it to close.
void test_many_connections()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    std::vector<int>      idle;
    std::jthread          browser { [&] {
        // More than the server handles at a time: the last ones wait their
        // turn, until the first ones expire or close.
        for (int ix = 0; ix < 20; ++ix) {
            idle.push_back(connect_to(server.port()));
        }
        for (int ix = 0; ix < 10; ++ix) {
            ::close(idle[ix]);
        }
        https_get(server.port(), "/callback?code=crowded&state=s");
    } };
    auto const parameters = server.wait_for_redirect("/callback", "s", 10s, { });
    browser.join();
    for (size_t ix = 10; ix < idle.size(); ++ix) {
        ::close(idle[ix]);
    }
    CHECK(parameters.has_value() && parameters->at("code") == "crowded");
}

void test_silent_client_does_not_block_the_login()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    int                   silent { -1 };
    std::jthread          browser { [&] {
        silent = connect_to(server.port());
        std::this_thread::sleep_for(100ms);
        https_get(server.port(), "/callback?code=after&state=s");
    } };
    auto const start = std::chrono::steady_clock::now();
    auto const parameters = server.wait_for_redirect("/callback", "s", 30s, { });
    auto const waited = std::chrono::steady_clock::now() - start;
    browser.join();
    ::close(silent);
    CHECK(parameters.has_value() && parameters->at("code") == "after");
    // Not held up by the silent one.
    CHECK(waited < 2s);
}

// Starts a TLS record that announces 16 KB, and sends it a byte at a time,
// every 100 ms, for `duration`: each byte comes well within the server's
// timeout for a connection.
std::jthread trickle(int port, std::chrono::milliseconds duration)
{
    return std::jthread { [port, duration] {
        int const  fd = connect_to(port);
        char const header[] = { 0x16, 0x03, 0x01, 0x40, 0x00 };
        ::write(fd, header, sizeof(header));
        auto const end = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(100ms);
            ::write(fd, "\x01", 1);
        }
        ::close(fd);
    } };
}

// A client that sends its handshake slowly mustn't keep the login waiting
// past its timeout.
void test_slow_client_and_the_timeout()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    auto                  client = trickle(server.port(), 2500ms);
    auto const            start = std::chrono::steady_clock::now();
    auto const            parameters = server.wait_for_redirect("/callback", "s", 1s, { });
    auto const            waited = std::chrono::steady_clock::now() - start;
    CHECK(!parameters.has_value() && parameters.error().kind == RedirectError::Kind::TimedOut);
    CHECK(waited < 1500ms);
}

// Nor past a stop request: when the app quits, it waits for the login's
// thread.
void test_slow_client_and_a_stop_request()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    std::stop_source      stop;
    auto                  client = trickle(server.port(), 2500ms);
    std::jthread          stopper { [&] {
        std::this_thread::sleep_for(300ms);
        stop.request_stop();
    } };
    auto const start = std::chrono::steady_clock::now();
    auto const parameters = server.wait_for_redirect("/callback", "s", 60s, stop.get_token());
    auto const waited = std::chrono::steady_clock::now() - start;
    CHECK(!parameters.has_value() && parameters.error().kind == RedirectError::Kind::Stopped);
    CHECK(waited < 1000ms);
}

void test_timeout()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    auto const            parameters = server.wait_for_redirect("/callback", "s", 1s, { });
    CHECK(!parameters.has_value() && parameters.error().kind == RedirectError::Kind::TimedOut);
}

void test_stop()
{
    TestCertificate const certificate;
    auto                  server = certificate.server();
    std::stop_source      stop;
    std::jthread          stopper { [&] {
        std::this_thread::sleep_for(100ms);
        stop.request_stop();
    } };
    auto const start = std::chrono::steady_clock::now();
    auto const parameters = server.wait_for_redirect("/callback", "s", 60s, stop.get_token());
    CHECK(!parameters.has_value() && parameters.error().kind == RedirectError::Kind::Stopped);
    CHECK(std::chrono::steady_clock::now() - start < 5s);
}

void test_taken_port()
{
    TestCertificate const certificate;
    auto const            server = certificate.server();
    auto const            second = LoopbackServer::listen(server.port(), MUST_EVAL(TlsContext::load(certificate.certificate, certificate.key)));
    CHECK(!second.has_value());
}

// What open_browser() runs the browser with.
void test_run_and_wait()
{
    CHECK_EQ(run_and_wait({ "sh", "-c", "exit 0" }).value_or(-1), 0);
    CHECK_EQ(run_and_wait({ "sh", "-c", "exit 3" }).value_or(-1), 3);
    CHECK_EQ(run_and_wait({ "sh", "-c", "sleep 0.1; exit 4" }).value_or(-1), 4);
    // An error on macOS, exit code 127 elsewhere.
    auto const missing = run_and_wait({ "sweattrails-no-such-program" });
    CHECK(!missing.has_value() || *missing != 0);
}

void test_missing_certificate()
{
    TestCertificate const certificate;
    CHECK(!TlsContext::load(certificate.dir.path / "missing.pem", certificate.key).has_value());
    CHECK(!TlsContext::load(certificate.certificate, certificate.dir.path / "missing.pem").has_value());
    // The certificate as its own key.
    CHECK(!TlsContext::load(certificate.certificate, certificate.certificate).has_value());
}

}

int main()
{
    // The tests' clients write to connections the server may have closed,
    // as when a login times out: that's an error to them, not the end.
    std::signal(SIGPIPE, SIG_IGN);
    test_challenge();
    test_verifier();
    test_parse_query();
    test_redirect();
    test_redirect_over_ipv6();
    test_denied_login();
    test_other_requests_are_not_the_redirect();
    test_failed_connections_are_dropped();
    test_silent_client_does_not_block_the_login();
    test_earlier_login_is_not_the_redirect();
    test_many_connections();
    test_slow_client_and_the_timeout();
    test_slow_client_and_a_stop_request();
    test_timeout();
    test_stop();
    test_taken_port();
    test_missing_certificate();
    test_run_and_wait();
    return ST::Test::test_result();
}
