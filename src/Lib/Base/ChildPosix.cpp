/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>
#include <csignal>
#include <format>
#include <mutex>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Child.h>

// The environment, for the programs the app starts: POSIX has no header
// that declares it.
extern char **environ;

namespace ST {

struct Child::State {
    pid_t      pid { -1 };
    int        stdin_fd { -1 };
    std::mutex mutex { };
    bool       reaped { false }; // After which the pid may be another process's
};

int process_id()
{
    return static_cast<int>(::getpid());
}

std::expected<Child, LibCError> Child::start(std::vector<std::string> const &command, Options const &options)
{
    auto state = std::make_unique<State>();

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    int pipe_fds[2] { -1, -1 };
    if (options.pipe_stdin) {
        // Writing to a program that has gone is an error from write(), not
        // the end of the app.
        std::signal(SIGPIPE, SIG_IGN);
        // Neither end goes to other programs the app starts, which would
        // keep the program's stdin open.
#ifdef __linux__
        auto const piped = ::pipe2(pipe_fds, O_CLOEXEC);
#else
        auto const piped = ::pipe(pipe_fds);
        if (piped == 0) {
            ::fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);
            ::fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC);
        }
#endif
        if (piped != 0) {
            posix_spawn_file_actions_destroy(&actions);
            return std::unexpected(LibCError { });
        }
        posix_spawn_file_actions_adddup2(&actions, pipe_fds[0], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    }
    if (options.discard_stdout) {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    }
    if (options.stderr_file) {
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, options.stderr_file->c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }

    std::vector<char *> argv;
    for (auto const &argument : command) {
        argv.push_back(const_cast<char *>(argument.c_str()));
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

    auto const spawned = ::posix_spawnp(&state->pid, argv[0], &actions, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (options.pipe_stdin) {
        ::close(pipe_fds[0]);
    }
    if (spawned != 0) {
        if (options.pipe_stdin) {
            ::close(pipe_fds[1]);
        }
        return std::unexpected(LibCError { spawned });
    }
    state->stdin_fd = pipe_fds[1];
    return Child { std::move(state) };
}

Child::Child(std::unique_ptr<State> state)
    : m_state(std::move(state))
{
}

Child::Child(Child &&) noexcept = default;
Child &Child::operator=(Child &&) noexcept = default;

Child::~Child()
{
    if (m_state) {
        close_stdin();
    }
}

bool Child::write(std::span<unsigned char const> bytes)
{
    while (!bytes.empty()) {
        auto const n = ::write(m_state->stdin_fd, bytes.data(), bytes.size());
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

void Child::close_stdin()
{
    if (m_state->stdin_fd >= 0) {
        ::close(m_state->stdin_fd);
        m_state->stdin_fd = -1;
    }
}

// The program is only reaped once kill() can't get at it, so that kill()
// never kills another process that got its pid.
std::expected<int, LibCError> Child::wait()
{
    close_stdin();
    siginfo_t info { };
    while (::waitid(P_PID, static_cast<id_t>(m_state->pid), &info, WEXITED | WNOWAIT) < 0) {
        if (errno != EINTR) {
            return std::unexpected(LibCError { });
        }
    }
    std::lock_guard lock { m_state->mutex };
    int             status { 0 };
    while (::waitpid(m_state->pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return std::unexpected(LibCError { });
        }
    }
    m_state->reaped = true;
    if (!WIFEXITED(status)) {
        return std::unexpected(LibCError { std::format("ended with signal {}", WTERMSIG(status)) });
    }
    return WEXITSTATUS(status);
}

void Child::kill()
{
    std::lock_guard lock { m_state->mutex };
    if (!m_state->reaped) {
        ::kill(m_state->pid, SIGKILL);
    }
}

}
