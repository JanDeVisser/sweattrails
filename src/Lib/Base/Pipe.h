/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <condition_variable>
#include <functional>
#include <string>
#include <string_view>
#include <sys/fcntl.h>
#include <sys/poll.h>
#include <thread>
#include <unistd.h>

#include <Defer.h>
#include <Expected.h>
#include <Logging.h>

namespace ST {

constexpr static int PipeEndRead = 0;
constexpr static int PipeEndWrite = 1;

template<typename T = void *>
class ReadPipe {
public:
    using OnRead = std::function<void(ReadPipe &)>;
    ReadPipe() = default;
    ReadPipe(OnRead on_read)
        : m_on_read(std::move(on_read))
    {
    }

    ReadPipe(ReadPipe const &) = delete;
    ReadPipe(ReadPipe &&) = delete;
    ~ReadPipe()
    {
        close();
    }

    CError initialize()
    {
        if (pipe(m_pipe) == -1) {
            return std::unexpected(LibCError());
        }
        return { };
    }

    Error<> initialize(T ctx, std::optional<OnRead> on_read)
    {
        m_context = std::move(ctx);
        m_on_read = std::move(on_read);
        return initialize();
    }

    Error<> connect(int fd)
    {
        m_fd = fd;
        if (fcntl(m_fd, F_SETFL, O_NONBLOCK) < 0) {
            return std::unexpected(LibCError());
        }

        std::thread thr = std::thread(&ReadPipe::read, this);
        thr.detach();
        return { };
    }

    void connect_parent()
    {
        MUST(connect(m_pipe[PipeEndRead]));
        ::close(m_pipe[PipeEndWrite]);
    }

    void connect_child(int fd) const
    {
        while ((dup2(m_pipe[PipeEndWrite], fd) == -1) && (errno == EINTR)) { }
        ::close(m_pipe[PipeEndRead]);
        ::close(m_pipe[PipeEndWrite]);
    }

    void close()
    {
        m_condition.notify_all();
        if (m_fd >= 0) {
            ::close(m_fd);
        }
        m_fd = -1;
    }

    bool expect()
    {
        std::unique_lock lk(m_mutex);
        m_condition.wait(lk, [this]() {
            return !m_current.empty() || m_fd < 0;
        });
        if (m_fd < 0) {
            return false;
        }
        return true;
    }

    std::string const current()
    {
        std::string ret;
        {
            std::unique_lock lk(m_mutex);
            Defer            sg { [&lk]() {
                lk.unlock();
            } };
            if (m_fd >= 0 && m_current.empty()) {
                m_condition.wait(lk, [this]() {
                    return !m_current.empty() || m_fd < 0;
                });
            }
            if (m_fd < 0) {
                return { };
            }
            ret = std::move(m_current);
            m_current = "";
        }
        return ret;
    }

    T &context()
    {
        return m_context;
    }

private:
    void read()
    {
        struct pollfd poll_fd = { .fd = 0, .events = 0, .revents = 0 };
        poll_fd.fd = m_fd;
        poll_fd.events = POLLIN;

        while (true) {
            if (poll(&poll_fd, 1, -1) == -1) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (poll_fd.revents & POLLIN) {
                MUST(drain());
            }
            if (poll_fd.revents & POLLHUP) {
                break;
            }
        }
        close();
    }

    constexpr static int DRAIN_SIZE = (64 * 1024);

    Error<> drain()
    {
        {
            std::unique_lock lk(m_mutex);
            Defer            guard { [&lk]() {
                lk.unlock();
            } };
            char buffer[DRAIN_SIZE];
            while (true) {
                ssize_t count = ::read(m_fd, buffer, sizeof(buffer) - 1);
                if (count >= 0) {
                    buffer[count] = 0;
                    if (count > 0) {
                        m_current.append(buffer, count);
                        if (count == sizeof(buffer) - 1) {
                            continue;
                        }
                    }
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                return std::unexpected(LibCError());
            }
            m_condition.notify_all();
        }
        if (m_on_read) {
            (*m_on_read)(*this);
        }
        return { };
    }

    int                     m_pipe[2] { 0, 0 };
    int                     m_fd { -1 };
    std::string             m_current { };
    std::mutex              m_mutex { };
    std::condition_variable m_condition { };
    std::optional<OnRead>   m_on_read { };
    T                       m_context { };
    bool                    m_debug { false };
};

class WritePipe {
public:
    WritePipe() = default;
    WritePipe(WritePipe const &) = delete;
    WritePipe(WritePipe &&from) = delete;

    ~WritePipe()
    {
        close();
    }

    Error<LibCError> initialize()
    {
        if (pipe(m_pipe) == -1) {
            return std::unexpected(LibCError());
        }
        return { };
    }

    void close()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
        m_fd = -1;
    }

    void connect_parent()
    {
        m_fd = m_pipe[PipeEndWrite];
        ::close(m_pipe[PipeEndRead]);
    }

    void connect_child(int fd) const
    {
        while ((dup2(m_pipe[PipeEndRead], fd) == -1) && (errno == EINTR)) { }
        ::close(m_pipe[PipeEndRead]);
        ::close(m_pipe[PipeEndWrite]);
    }

    template<typename... Args>
    CError write(std::string_view const &fmt, Args const &...args) const
    {
        auto s = std::format(fmt, std::make_format_args<Args...>(args...));
        return write_chars(s.data(), s.length());
    }

    CError write_chars(char const *buf, size_t num) const
    {
        size_t total = { 0 };
        while (total < num) {
            ssize_t count = ::write(m_fd, buf + total, num - total);
            if (count < 0) {
                if (errno != EINTR) {
                    return std::unexpected(LibCError());
                }
                continue;
            }
            total += count;
        }
        return { };
    }

private:
    int m_pipe[2];
    int m_fd;
};

}
