/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>
#include <csignal>
#include <cstring>
#include <format>
#include <fstream>
#include <span>
#include <sstream>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <overlay/Encoder.h>
#include <overlay/Render.h>

extern char **environ;

namespace ST {

namespace {

// All of `bytes`, or false if the reader has gone.
bool write_all(int fd, std::span<unsigned char const> bytes)
{
    while (!bytes.empty()) {
        auto const n = ::write(fd, bytes.data(), bytes.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        bytes = bytes.subspan(static_cast<size_t>(n));
    }
    return true;
}

std::string read_log(fs::path const &log)
{
    std::ifstream     in { log };
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

}

std::string last_line(std::string_view log)
{
    auto const end = log.find_last_not_of("\r\n \t");
    if (end == std::string_view::npos) {
        return { };
    }
    auto const trimmed = log.substr(0, end + 1);
    auto const begin = trimmed.find_last_of("\r\n");
    return std::string { (begin == std::string_view::npos) ? trimmed : trimmed.substr(begin + 1) };
}

Encoder::Encoder(fs::path output, fs::path log, size_t frame_bytes)
    : m_output(std::move(output))
    , m_log(std::move(log))
    , m_frame_bytes(frame_bytes)
{
}

std::expected<std::unique_ptr<Encoder>, std::string> Encoder::start(
    fs::path const &program, std::vector<std::string> const &args, fs::path output, size_t frame_bytes)
{
    // Writing to a program that has gone is an error from write(), not the
    // end of the app.
    std::signal(SIGPIPE, SIG_IGN);

    auto const               log = fs::temp_directory_path() / std::format("sweattrails-{}-{}.log", ::getpid(), output.stem().string());
    std::unique_ptr<Encoder> encoder { new Encoder(std::move(output), log, frame_bytes) };

    // Neither end goes to other programs the app starts, which would keep
    // the program's stdin open.
    int fds[2];
#ifdef __linux__
    auto const piped = ::pipe2(fds, O_CLOEXEC);
#else
    auto const piped = ::pipe(fds);
    if (piped == 0) {
        ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }
#endif
    if (piped != 0) {
        return std::unexpected(std::format("Can't start {}: {}", program.filename().string(), std::strerror(errno)));
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

    std::vector<std::string> strings { program.string() };
    strings.append_range(args);
    std::vector<char *> argv;
    for (auto &s : strings) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);

    // The program gets its stdin, stdout and stderr, and none of the app's
    // other files, such as the Wahoo login's socket. Elsewhere it's up to
    // the app to open its files close-on-exec.
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif

    auto const spawned = posix_spawnp(&encoder->m_pid, program.c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[0]);
    if (spawned != 0) {
        std::error_code ignored;
        fs::remove(log, ignored);
        ::close(fds[1]);
        encoder->m_pid = -1;
        encoder->m_done = true;
        return std::unexpected(std::format("Can't start {}: {}", program.filename().string(), std::strerror(spawned)));
    }
    encoder->m_stdin = fds[1];
    encoder->m_writer = std::thread { [e = encoder.get()]() { e->write_frames(); } };
    return encoder;
}

Encoder::~Encoder()
{
    if (!m_done) {
        stop();
    }
}

Frame Encoder::buffer()
{
    std::lock_guard lock { m_mutex };
    if (m_spare.empty()) {
        return Frame(m_frame_bytes);
    }
    auto ret = std::move(m_spare.back());
    m_spare.pop_back();
    ret.resize(m_frame_bytes);
    return ret;
}

bool Encoder::can_write() const
{
    std::lock_guard lock { m_mutex };
    return m_queue.size() < QueueSize && !m_failed;
}

bool Encoder::failed() const
{
    std::lock_guard lock { m_mutex };
    return m_failed;
}

bool Encoder::write(Frame frame)
{
    std::unique_lock lock { m_mutex };
    m_changed.wait(lock, [this]() { return m_queue.size() < QueueSize || m_failed; });
    if (m_failed) {
        return false;
    }
    m_queue.push_back(std::move(frame));
    m_changed.notify_all();
    return true;
}

void Encoder::write_frames()
{
    while (true) {
        Frame frame;
        {
            std::unique_lock lock { m_mutex };
            m_changed.wait(lock, [this]() { return !m_queue.empty() || m_closing || m_stopping; });
            if (m_queue.empty() || m_stopping) {
                return;
            }
            frame = std::move(m_queue.front());
        }
        unpremultiply(frame);
        auto const      written = write_all(m_stdin, frame);
        std::lock_guard lock { m_mutex };
        m_queue.pop_front();
        m_spare.push_back(std::move(frame));
        if (!written) {
            m_failed = true;
            m_queue.clear();
        }
        m_changed.notify_all();
        if (m_failed) {
            return;
        }
    }
}

std::expected<void, std::string> Encoder::finish()
{
    if (m_done) {
        return { };
    }
    {
        std::lock_guard lock { m_mutex };
        m_closing = true;
        m_changed.notify_all();
    }
    m_writer.join();
    auto            ret = wait();
    std::error_code ignored;
    if (!ret) {
        fs::remove(m_output, ignored);
    }
    fs::remove(m_log, ignored);
    return ret;
}

void Encoder::stop()
{
    if (m_done) {
        return;
    }
    // The queue is the writer's to empty: it may be writing its front.
    {
        std::lock_guard lock { m_mutex };
        m_stopping = true;
        m_changed.notify_all();
    }
    // The writer may be in the middle of a frame; once the program has
    // gone that write fails.
    ::kill(m_pid, SIGKILL);
    m_writer.join();
    (void) wait();
    std::error_code ignored;
    fs::remove(m_output, ignored);
    fs::remove(m_log, ignored);
}

// Closes the program's stdin, and waits for it to exit.
std::expected<void, std::string> Encoder::wait()
{
    ::close(m_stdin);
    m_stdin = -1;
    m_done = true;
    int status { 0 };
    while (::waitpid(m_pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return std::unexpected(std::format("Can't wait for the encoder: {}", std::strerror(errno)));
        }
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && !m_failed) {
        return { };
    }
    if (auto const line = last_line(read_log(m_log)); !line.empty()) {
        return std::unexpected(line);
    }
    if (WIFEXITED(status)) {
        return std::unexpected(std::format("The encoder stopped with exit code {}", WEXITSTATUS(status)));
    }
    return std::unexpected("The encoder stopped");
}

}
