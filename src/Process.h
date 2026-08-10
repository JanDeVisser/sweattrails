/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <print>
#include <sys/wait.h>

#include <Error.h>
#include <Pipe.h>
#include <Result.h>
#include <StringUtil.h>

extern "C" {
void sigchld(int);
}

namespace ST {

template<class T = void *>
class Process {
public:
    using OnRead = typename ReadPipe<T>::OnRead;

    Process(std::string_view const &cmd) noexcept
        : m_command(cmd)
    {
    }

    template<typename... Args>
    Process(std::string_view const &cmd, Args &&...args)
        : Process(cmd)
    {
        StringList cmd_args = { };
        set_arguments(cmd_args, std::forward<Args>(args)...);
    }

    ~Process()
    {
    }

    pid_t pid() const { return m_pid; }

    Process &on_stdout_read(OnRead const &on_read)
    {
        m_on_stdout_read = on_read;
        return *this;
    }

    Process &on_stderr_read(OnRead const &on_read)
    {
        m_on_stderr_read = on_read;
        return *this;
    }

    CError start(T const &ctx)
    {
        std::println("[CMD] {} {}", m_command, join(m_arguments, " "));
        signal(SIGCHLD, sigchld);
        size_t sz = m_arguments.size();
        size_t bufsz = m_command.length() + 1;
        for (size_t ix = 0u; ix < sz; ++ix) {
            bufsz += m_arguments[ix].length() + 1;
        }
        char        buf[bufsz];
        char const *argv[sz + 2];
        argv[0] = m_command.c_str();
        strcpy(buf, argv[0]);
        char *bufptr = buf + m_command.length() + 1;
        for (size_t ix = 0u; ix < sz; ++ix) {
            argv[ix + 1] = m_arguments[ix].c_str();
            bufptr = bufptr + m_arguments[ix].length() + 1;
        }
        argv[sz + 1] = NULL;

        // signal(SIGCHLD, SIG_IGN);
        if (auto err = m_in.initialize(); err) {
            return err;
        }
        if (auto err = m_out.initialize(ctx, m_on_stdout_read); err) {
            return err;
        }
        if (auto err = m_err.initialize(ctx, m_on_stderr_read); err) {
            return err;
        }

        m_pid = fork();
        if (m_pid == -1) {
            return LibCError(errno);
        }
        if (m_pid == 0) {
            m_in.connect_child(STDIN_FILENO);

            auto connect_stream = [this](ReadPipe<T> &pipe, int fileno, std::string const &redirect) {
                if (redirect.empty()) {
                    pipe.connect_child(fileno);
                } else {
                    int fd = open(redirect.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0777);
                    assert(fd);
                    while (dup2(fd, fileno) == -1 && (errno == EINTR)) { }
                }
            };
            connect_stream(m_out, STDOUT_FILENO, stdout_file);
            connect_stream(m_err, STDERR_FILENO, stderr_file);
            ::execvp(argv[0], (char *const *) argv);
            fatal("execvp({}) failed: {}", m_command, LibCError(errno).description);
        }
        m_in.connect_parent();
        m_out.connect_parent();
        m_err.connect_parent();
        return { };
    }

    CError background(T const &ctx)
    {
        return start(ctx);
    }

    Result<int> wait()
    {
        if (m_pid == 0) {
            return { 0 };
        }
        int exit_code;
        if (waitpid(m_pid, &exit_code, 0) == -1 && errno != ECHILD && errno != EINTR) {
            return LibCError();
        }
        m_pid = 0;
        m_in.close();
        if (!WIFEXITED(exit_code)) {
            LibCError("Child program {} crashed due to signal {}", m_command, WTERMSIG(exit_code));
        }
        return { WEXITSTATUS(exit_code) };
    }

    CError start()
    {
        T ctx { };
        return start(ctx);
    }

    Result<int> execute()
    {
        TRY(start());
        return wait();
    }

    CError write_to(std::string_view const &msg)
    {
        TRY(m_in.write(msg));
        return { };
    }

    std::string stdout_file;
    std::string stderr_file;

private:
    void set_arguments(StringList const &cmd_args)
    {
        m_arguments = cmd_args;
    }

    template<typename... Args>
    void set_arguments(StringList &cmd_args, std::string_view const &arg, Args &&...args)
    {
        cmd_args.emplace_back(arg);
        set_arguments(cmd_args, std::forward<Args>(args)...);
    }

    pid_t                    m_pid { -1 };
    std::string              m_command;
    std::vector<std::string> m_arguments;
    WritePipe                m_in { };
    ReadPipe<T>              m_out { };
    ReadPipe<T>              m_err { };
    std::optional<OnRead>    m_on_stdout_read { };
    std::optional<OnRead>    m_on_stderr_read { };
};

}
