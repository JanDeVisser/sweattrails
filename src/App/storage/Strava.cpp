/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * C++ translation of zig/connect/strava.zig.
 */

#include <cassert>
#include <cctype>
#include <cerrno>
#include <format>
#include <system_error>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <IO.h>
#include <Logging.h>
#include <Process.h>
#include <StringUtil.h>

#include "Strava.h"

namespace ST {

namespace {

struct CurlTempFiles {
    std::string body_file;
    std::string status_file;

    static CurlTempFiles make()
    {
        auto const stamp = ST::now();
        return {
            .body_file = (fs::temp_directory_path() / std::format("sweattrails-strava-{}-body.json", stamp)).string(),
            .status_file = (fs::temp_directory_path() / std::format("sweattrails-strava-{}-status.txt", stamp)).string(),
        };
    }
};

// curl's `-o <file>` writes the response body directly, and `-w '%{http_code}'`
// (combined with `-s`, which silences everything else curl would otherwise
// print) writes just the HTTP status code to stdout. Process::stdout_file
// redirects that via dup2() in the child process (see Process::start()), so
// unlike ReadPipe there is no background thread to race against; the file is
// simply there once execute() returns. This is the same approach used for
// tile fetching in ../Map.cpp.
StravaResult<std::string> curl_finish(Process<> &curl, CurlTempFiles const &files, std::string_view const &url)
{
    curl.stdout_file = files.status_file;
    auto exit_code = curl.execute();
    if (!exit_code) {
        return std::unexpected(StravaError { exit_code.error() });
    }
    if (*exit_code != 0) {
        return std::unexpected(StravaError { LibCError { std::format("curl exited with {} fetching `{}`", *exit_code, url) } });
    }
    auto            status = read_file_by_name(files.status_file);
    auto            body = read_file_by_name(files.body_file);
    std::error_code ec { };
    fs::remove(files.status_file, ec);
    fs::remove(files.body_file, ec);
    if (!status) {
        return std::unexpected(StravaError { status.error() });
    }
    if (*status != "200") {
        return std::unexpected(StravaError { LibCError { std::format("`{}` returned HTTP status {}", url, *status) } });
    }
    if (!body) {
        return std::unexpected(StravaError { body.error() });
    }
    return *body;
}

StravaResult<std::string> curl_get(std::string_view const &url, std::string_view const &bearer_token)
{
    auto const files = CurlTempFiles::make();
    auto const auth_header = std::format("Authorization: Bearer {}", bearer_token);
    Process<>  curl {
        "curl",
        "-s",
        "-S",
        "-o",
        files.body_file,
        "-w",
        "%{http_code}",
        "-A",
        "sweattrails/1.0",
        "-H",
        auth_header,
        std::string { url },
    };
    return curl_finish(curl, files, url);
}

StravaResult<std::string> curl_post(std::string_view const &url, std::string_view const &payload)
{
    auto const files = CurlTempFiles::make();
    Process<>  curl {
        "curl",
        "-s",
        "-S",
        "-o",
        files.body_file,
        "-w",
        "%{http_code}",
        "-A",
        "sweattrails/1.0",
        "-X",
        "POST",
        "--data",
        std::string { payload },
        std::string { url },
    };
    return curl_finish(curl, files, url);
}

// ST::Socket::listen(ip_address, port) (src/IO.h) is not implemented
// (`fatal("listen(ip_addr, port) not yet implemented")` in IO.cpp), so the
// OAuth redirect callback is handled with a small raw-socket server here,
// mirroring what zig's std.Io.net did: bind, accept once, read the request
// line, answer with a canned HTML page, and pull `code=...` out of the
// request line.
StravaResult<std::string> wait_for_oauth_callback(int port)
{
    int const listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return std::unexpected(StravaError { LibCError { } });
    }
    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr { };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        LibCError const err { };
        ::close(listen_fd);
        return std::unexpected(StravaError { err });
    }
    if (::listen(listen_fd, 1) < 0) {
        LibCError const err { };
        ::close(listen_fd);
        return std::unexpected(StravaError { err });
    }

    int const conn_fd = ::accept(listen_fd, nullptr, nullptr);
    ::close(listen_fd);
    if (conn_fd < 0) {
        return std::unexpected(StravaError { LibCError { } });
    }

    std::string request_line;
    char        buf[4096];
    while (request_line.find('\n') == std::string::npos) {
        auto const count = ::read(conn_fd, buf, sizeof(buf));
        if (count <= 0) {
            break;
        }
        request_line.append(buf, static_cast<size_t>(count));
    }
    info(Strava, "auth callback: {}", request_line);

    static constexpr std::string_view response = "HTTP/1.1 200 OK\r\n"
                                                 "Content-Type: text/html\r\n"
                                                 "Connection: close\r\n"
                                                 "\r\n"
                                                 "<html><body><h1>Authorization successful!</h1>\r\n"
                                                 "<p>You can close this window and return to SweatTrails.</p></body></html>\r\n"
                                                 "\r\n";
    ::write(conn_fd, response.data(), response.length());
    ::close(conn_fd);

    auto const code_start = request_line.find("code=");
    if (code_start == std::string::npos) {
        return std::unexpected(StravaError { StravaError::Code::NoAuthCodeReceived });
    }
    auto code_end = code_start + 5;
    while (code_end < request_line.length() && request_line[code_end] != '&' && !std::isspace(static_cast<unsigned char>(request_line[code_end]))) {
        ++code_end;
    }
    return request_line.substr(code_start + 5, code_end - (code_start + 5));
}

}

std::string StravaError::to_string() const
{
    return std::visit(
        overloaded {
            [](LibCError const &e) { return e.to_string(); },
            [](JSONError const &e) { return e.to_string(); },
            [](Code const &c) -> std::string {
                switch (c) {
                case Code::InvalidConfigFile:
                    return "Strava config file is invalid (missing client_id/client_secret)";
                case Code::NoAuthCodeReceived:
                    return "No authorization code received from Strava";
                case Code::RequestError:
                    return "Strava request failed";
                case Code::InvalidAuthenticationTokenResponse:
                    return "Strava returned an invalid authentication token response";
                }
                return "Unknown Strava error";
            },
        },
        error);
}

Decoded<Strava::Config> Strava::Config::decode(JSONValue const &json)
{
    if (!json.is_object()) {
        return std::unexpected(JSONError { JSONError::Code::TypeMismatch, "Expected a JSON object for Strava::Config" });
    }
    auto get_string = [&json](std::string_view const &key) -> Decoded<std::string> {
        if (auto v = json.get(key); v) {
            std::string s;
            TRY(v->convert(s));
            return s;
        }
        return std::string { };
    };
    auto get_u64 = [&json](std::string_view const &key) -> Decoded<u64> {
        if (auto v = json.get(key); v) {
            u64 n = 0;
            TRY(v->convert(n));
            return n;
        }
        return u64 { 0 };
    };

    Config ret { };
    ret.client_id = TRY_EVAL(get_string("client_id"));
    ret.client_secret = TRY_EVAL(get_string("client_secret"));
    ret.access_token = TRY_EVAL(get_string("access_token"));
    ret.refresh_token = TRY_EVAL(get_string("refresh_token"));
    ret.expires_at = TRY_EVAL(get_u64("expires_at"));
    ret.newest_synced = TRY_EVAL(get_u64("newest_synced"));
    ret.oldest_synced = TRY_EVAL(get_u64("oldest_synced"));
    return ret;
}

JSONValue Strava::Config::encode() const
{
    auto obj = JSONValue::object();
    ST::set(obj, "client_id", client_id);
    ST::set(obj, "client_secret", client_secret);
    ST::set(obj, "access_token", access_token);
    ST::set(obj, "refresh_token", refresh_token);
    ST::set(obj, "expires_at", expires_at);
    ST::set(obj, "newest_synced", newest_synced);
    ST::set(obj, "oldest_synced", oldest_synced);
    return obj;
}

StravaResult<std::optional<Strava>> Strava::init(fs::path const &data_dir)
{
    auto const config_path = (data_dir / "strava.config").string();
    if (!fs::exists(config_path)) {
        return std::optional<Strava> { };
    }
    auto text = read_file_by_name(config_path);
    if (!text) {
        return std::unexpected(StravaError { text.error() });
    }
    auto json = JSONValue::deserialize(*text);
    if (!json) {
        return std::unexpected(StravaError { json.error() });
    }
    auto config = Config::decode(*json);
    if (!config) {
        return std::unexpected(StravaError { config.error() });
    }
    if (config->client_id.empty() || config->client_secret.empty()) {
        return std::unexpected(StravaError { StravaError::Code::InvalidConfigFile });
    }

    Strava ret { };
    ret.data_dir = data_dir;
    ret.config = *config;
    return std::optional<Strava> { std::move(ret) };
}

Error<StravaError> Strava::write_config()
{
    auto const text = std::format("{}\n", config.encode().serialize(true));
    auto       res = write_file_by_name((data_dir / "strava.config").string(), std::string_view { text });
    if (!res) {
        return std::unexpected(StravaError { res.error() });
    }
    return { };
}

Error<StravaError> Strava::parse_token_response(std::string_view const &resp)
{
    auto json = JSONValue::deserialize(resp);
    if (!json) {
        return std::unexpected(StravaError { json.error() });
    }
    auto value = Config::decode(*json);
    if (!value) {
        return std::unexpected(StravaError { value.error() });
    }
    if (value->access_token.empty() || value->refresh_token.empty() || value->expires_at == 0) {
        return std::unexpected(StravaError { StravaError::Code::InvalidAuthenticationTokenResponse });
    }
    config.access_token = value->access_token;
    config.refresh_token = value->refresh_token;
    config.expires_at = value->expires_at;
    return { };
}

StravaResult<std::string> Strava::request_response(std::string_view const &url)
{
    info(Strava, "GET {}", url);
    auto response = curl_get(url, config.access_token);
    if (!response) {
        log_error("GET {} failed: {}", url, response.error().to_string());
        return std::unexpected(StravaError { StravaError::Code::RequestError });
    }
    return *response;
}

Error<StravaError> Strava::populate_pending_list(std::string_view const &filter)
{
    auto const url = std::format("{}/athlete/activities?per_page=200{}", STRAVA_API_URL, filter);
    auto       activities = TRY_EVAL(request<JSONValue>(url));
    assert(activities.is_array());
    for (auto const &activity : activities) {
        assert(activity.is_object());
        auto const id = TRY_EVAL(activity.try_get<i64>("id"));
        auto const name = activity.get("name");
        auto const start_date = activity.get("start_date");
        info(Strava, "{} {} {}",
            id,
            name ? name->to_string() : std::string { },
            start_date ? start_date->to_string() : std::string { });
        pending.push_back(id);
    }
    return { };
}

Error<StravaError> Strava::authenticate()
{
    if (config.access_token.empty() || config.refresh_token.empty() || config.expires_at == 0) {
        auto const auth_url = std::format(
            "{}?client_id={}&response_type=code&redirect_uri={}&approval_prompt=auto&scope=activity:read_all",
            STRAVA_AUTH_URL, config.client_id, REDIRECT_URI);
        info(Strava, "Opening browser for Strava authorization...");
        info(Strava, "If browser doesn't open, visit:\n{}\n", auth_url);

        // macOS-specific, matching the zig source
        // (`std.process.run(..., .{ .argv = &.{ "open", auth_url } })`).
        Process<> open_browser { "open", auth_url };
        if (auto res = open_browser.execute(); !res) {
            return std::unexpected(StravaError { res.error() });
        }

        auto code = TRY_EVAL(wait_for_oauth_callback(CALLBACK_PORT));
        info(Strava, "code: {}", code);

        auto const post_data = std::format(
            "client_id={}&client_secret={}&code={}&grant_type=authorization_code",
            config.client_id, config.client_secret, code);
        auto response = TRY_EVAL(curl_post(STRAVA_TOKEN_URL, post_data));
        info(Strava, "fetched auth token: {}", response);
        TRY(parse_token_response(response));
        TRY(write_config());
    }

    if (config.expires_at < static_cast<u64>(now()) + 300) {
        auto const post_data = std::format(
            "client_id={}&client_secret={}&refresh_token={}&grant_type=refresh_token",
            config.client_id, config.client_secret, config.refresh_token);
        auto response = TRY_EVAL(curl_post(STRAVA_TOKEN_URL, post_data));
        info(Strava, "re-fetched auth token: {}", response);
        TRY(parse_token_response(response));
        TRY(write_config());
    }
    return { };
}

Error<StravaError> Strava::get_next_activity()
{
    TRY(authenticate());

    if (pending.empty() && config.newest_synced > 0) {
        TRY(populate_pending_list(std::format("&after={}", config.newest_synced)));
    }
    if (pending.empty()) {
        if (config.oldest_synced > 0) {
            TRY(populate_pending_list(std::format("&before={}", config.oldest_synced)));
        } else {
            TRY(populate_pending_list(""));
        }
    }
    if (!pending.empty()) {
        auto const id = pending.front();
        pending.pop_front();
        auto const url = std::format("{}/activities/{}", STRAVA_API_URL, id);
        auto       activity = TRY_EVAL(request<JSONValue>(url));
        auto const start_date = TRY_EVAL(activity.try_get<std::string>("start_date"));
        auto       d = DateTime::parse_iso8601(start_date);
        if (!d) {
            return std::unexpected(StravaError { d.error() });
        }
        auto const      activity_json = activity.serialize();
        auto const      dest_dir_name = std::format("activity/{:04}/{:02}", static_cast<u16>(d->year), static_cast<u8>(d->month) + 1);
        auto const      dest_dir = data_dir / dest_dir_name;
        std::error_code ec { };
        fs::create_directories(dest_dir, ec);
        if (ec) {
            return std::unexpected(StravaError { LibCError { ec.message() } });
        }
        auto const fname = std::format("{}.strava", d->timestamp);
        info(Strava, "Writing {}/{}", dest_dir_name, fname);
        auto write_res = write_file_by_name((dest_dir / fname).string(), std::string_view { activity_json });
        if (!write_res) {
            return std::unexpected(StravaError { write_res.error() });
        }
        bool write_cfg = false;
        if (d->timestamp > config.newest_synced) {
            config.newest_synced = d->timestamp;
            write_cfg = true;
        }
        if (config.oldest_synced == 0 || d->timestamp < config.oldest_synced) {
            config.oldest_synced = d->timestamp;
            write_cfg = true;
        }
        if (write_cfg) {
            TRY(write_config());
        }
    }
    return { };
}

}
