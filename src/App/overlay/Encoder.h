/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Feeds frames to ffmpeg. See specs/video-overlay.md.
 */

#pragma once

#include <condition_variable>
#include <deque>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <sys/types.h>

namespace ST {

namespace fs = std::filesystem;

using Frame = std::vector<unsigned char>;

// The last line of a log, where ffmpeg puts its error. Progress lines end
// in a carriage return.
std::string last_line(std::string_view log);

// A program, such as ffmpeg, that gets frames of premultiplied RGBA on its
// stdin, and writes `output`. The frames are made straight alpha and
// written on a thread of the encoder's own, so that rendering doesn't wait
// for the encoding, until two frames are waiting.
class Encoder {
public:
    static constexpr size_t QueueSize = 2;

    // `args` are the program's arguments, without the program itself.
    static std::expected<std::unique_ptr<Encoder>, std::string> start(
        fs::path const &program, std::vector<std::string> const &args, fs::path output, size_t frame_bytes);

    Encoder(Encoder const &) = delete;
    Encoder &operator=(Encoder const &) = delete;
    ~Encoder(); // Stops, unless finished.

    // A buffer for the next frame, one the encoder is done with if there is
    // one, sized for a frame.
    [[nodiscard]] Frame buffer();
    // Whether write() can take a frame without waiting.
    [[nodiscard]] bool can_write() const;
    // Whether the program has gone before all frames were written.
    [[nodiscard]] bool failed() const;
    // Queues a frame; waits while the queue is full. An error once the
    // program has gone, after which finish() tells why.
    bool write(Frame frame);
    // There are no more frames: once they're written, the program's stdin
    // is closed, and the encoder waits for it, on its own thread. On an
    // error the output is removed.
    void close();
    // How it went, once the program has finished.
    [[nodiscard]] std::optional<std::expected<void, std::string>> result() const;
    // Closes, and waits until the program has finished.
    std::expected<void, std::string> finish();
    // Kills the program and removes the output.
    void stop();

    [[nodiscard]] fs::path const &output() const { return m_output; }

private:
    Encoder(fs::path output, fs::path log, size_t frame_bytes);
    void                             write_frames();
    std::expected<void, std::string> wait();

    fs::path    m_output;
    fs::path    m_log;
    size_t      m_frame_bytes;
    pid_t       m_pid { -1 };
    int         m_stdin { -1 };
    std::thread m_writer;

    mutable std::mutex                              m_mutex;
    std::condition_variable                         m_changed;
    std::deque<Frame>                               m_queue;
    std::vector<Frame>                              m_spare;
    bool                                            m_closing { false };  // No more frames come
    bool                                            m_stopping { false }; // Drop the frames left
    bool                                            m_failed { false };
    bool                                            m_exited { false }; // The program has, and may be reaped
    std::optional<std::expected<void, std::string>> m_result { };

    bool m_joined { false }; // The writer has finished. Only used by the encoder's owner
};

}
