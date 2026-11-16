/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <format>
#include <fstream>
#include <sstream>

#include <overlay/Encoder.h>
#include <overlay/Render.h>

namespace ST {

namespace {

std::string read_log(fs::path const &log)
{
    std::ifstream     in { log, std::ios::binary };
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

Encoder::Encoder(Child child, fs::path output, fs::path log, size_t frame_bytes)
    : m_output(std::move(output))
    , m_log(std::move(log))
    , m_frame_bytes(frame_bytes)
    , m_child(std::move(child))
{
}

std::expected<std::unique_ptr<Encoder>, std::string> Encoder::start(
    fs::path const &program, std::vector<std::string> const &args, fs::path output, size_t frame_bytes)
{
    auto const               log = fs::temp_directory_path() / std::format("sweattrails-{}-{}.log", process_id(), output.stem().string());
    std::vector<std::string> command { program.string() };
    command.append_range(args);
    auto child = Child::start(command, Child::Options { .pipe_stdin = true, .discard_stdout = true, .stderr_file = log });
    if (!child) {
        std::error_code ignored;
        fs::remove(log, ignored);
        return std::unexpected(std::format("Can't start {}: {}", program.filename().string(), child.error().description));
    }
    std::unique_ptr<Encoder> encoder { new Encoder(std::move(*child), std::move(output), log, frame_bytes) };
    encoder->m_writer = std::thread { [e = encoder.get()]() { e->write_frames(); } };
    return encoder;
}

Encoder::~Encoder()
{
    if (!m_joined) {
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

// Writes the frames until there are no more, or the program has gone, and
// then waits for the program, so that the UI thread doesn't.
void Encoder::write_frames()
{
    while (true) {
        Frame frame;
        {
            std::unique_lock lock { m_mutex };
            m_changed.wait(lock, [this]() { return !m_queue.empty() || m_closing || m_stopping; });
            if (m_queue.empty() || m_stopping) {
                break;
            }
            frame = std::move(m_queue.front());
        }
        unpremultiply(frame);
        auto const      written = m_child.write(frame);
        std::lock_guard lock { m_mutex };
        m_queue.pop_front();
        m_spare.push_back(std::move(frame));
        if (!written) {
            m_failed = true;
            m_queue.clear();
        }
        m_changed.notify_all();
        if (m_failed) {
            break;
        }
    }
    auto const      result = wait();
    std::error_code ignored;
    if (!result) {
        fs::remove(m_output, ignored);
    }
    fs::remove(m_log, ignored);
    std::lock_guard lock { m_mutex };
    m_result = result;
}

void Encoder::close()
{
    std::lock_guard lock { m_mutex };
    m_closing = true;
    m_changed.notify_all();
}

std::optional<std::expected<void, std::string>> Encoder::result() const
{
    std::lock_guard lock { m_mutex };
    return m_result;
}

std::expected<void, std::string> Encoder::finish()
{
    if (!m_joined) {
        close();
        m_writer.join();
        m_joined = true;
    }
    std::lock_guard lock { m_mutex };
    return m_result.value_or(std::unexpected("Stopped"));
}

void Encoder::stop()
{
    if (m_joined) {
        return;
    }
    // The queue is the writer's to empty: it may be writing its front.
    // Once the program has gone, that write fails.
    {
        std::lock_guard lock { m_mutex };
        m_stopping = true;
        m_child.kill();
        m_changed.notify_all();
    }
    m_writer.join();
    m_joined = true;
    std::error_code ignored;
    fs::remove(m_output, ignored);
    {
        std::lock_guard lock { m_mutex };
        m_result = std::unexpected("Stopped");
    }
}

// Closes the program's stdin, and waits for it to exit.
std::expected<void, std::string> Encoder::wait()
{
    auto const exit_code = m_child.wait();
    bool       failed { false };
    {
        std::lock_guard lock { m_mutex };
        failed = m_failed;
    }
    if (exit_code && *exit_code == 0 && !failed) {
        return { };
    }
    if (auto const line = last_line(read_log(m_log)); !line.empty()) {
        return std::unexpected(line);
    }
    if (exit_code) {
        return std::unexpected(std::format("The encoder stopped with exit code {}", *exit_code));
    }
    return std::unexpected("The encoder stopped");
}

}
