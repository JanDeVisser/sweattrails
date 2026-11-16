/*
 * Copyright (c) 2026, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <mutex>

#include <Child.h>
#include <Win32.h>

namespace ST {

struct Child::State {
    HANDLE     process { nullptr };
    HANDLE     stdin_pipe { nullptr };
    std::mutex mutex { };
    bool       waited { false };

    State() = default;
    State(State const &) = delete;
    State &operator=(State const &) = delete;

    // So that each way out of start() closes what it opened.
    ~State()
    {
        if (stdin_pipe != nullptr) {
            CloseHandle(stdin_pipe);
        }
        if (process != nullptr) {
            CloseHandle(process);
        }
    }
};

namespace {

// `argument` as the program's C runtime will read it back from the
// command line: quoted if it has spaces or quotes, with the backslashes
// before a quote, and before the closing quote, doubled.
std::string quote(std::string const &argument)
{
    if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string::npos) {
        return argument;
    }
    std::string ret { "\"" };
    size_t      backslashes { 0 };
    for (auto const ch : argument) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            ret.append(2 * backslashes + 1, '\\');
        } else {
            ret.append(backslashes, '\\');
        }
        backslashes = 0;
        ret += ch;
    }
    ret.append(2 * backslashes, '\\');
    ret += '"';
    return ret;
}

std::string command_line(std::vector<std::string> const &command)
{
    std::string ret;
    for (auto const &argument : command) {
        if (!ret.empty()) {
            ret += ' ';
        }
        ret += quote(argument);
    }
    return ret;
}

// The handles are made without inheritance, and only the program's are
// made inheritable, just before it starts. Another thread that starts a
// program meanwhile, such as with ShellExecuteW, never gets the app's end
// of the pipe, which would keep the program from seeing its stdin close.

HANDLE open_file(std::wstring const &path, DWORD access, DWORD disposition)
{
    return CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
}

// One of the app's standard handles, for the program: a copy of it, or the
// null device if the app has none, as a GUI app may not.
HANDLE std_copy(DWORD which, DWORD access)
{
    auto const handle = GetStdHandle(which);
    HANDLE     copy { nullptr };
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE
        && DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return copy;
    }
    return open_file(L"NUL", access, OPEN_EXISTING);
}

// The program's ends of its stdin, stdout and stderr, which the app closes
// once the program has them.
struct StdHandles {
    HANDLE handles[3] { nullptr, nullptr, nullptr };

    StdHandles() = default;
    StdHandles(StdHandles const &) = delete;
    StdHandles &operator=(StdHandles const &) = delete;

    ~StdHandles()
    {
        for (auto const handle : handles) {
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
            }
        }
    }

    [[nodiscard]] bool valid() const
    {
        for (auto const handle : handles) {
            if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool make_inheritable() const
    {
        for (auto const handle : handles) {
            if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
                return false;
            }
        }
        return true;
    }
};

}

int process_id()
{
    return static_cast<int>(GetCurrentProcessId());
}

std::expected<Child, LibCError> Child::start(std::vector<std::string> const &command, Options const &options)
{
    auto state = std::make_unique<State>();

    // Without redirection, the program shares the app's console, if any,
    // and inherits nothing.
    auto const redirect = options.pipe_stdin || options.discard_stdout || options.stderr_file.has_value();
    StdHandles std_handles;
    if (redirect) {
        auto &[in, out, err] = std_handles.handles;
        if (options.pipe_stdin) {
            if (!CreatePipe(&in, &state->stdin_pipe, nullptr, 0)) {
                return std::unexpected(win32_error());
            }
        } else {
            in = std_copy(STD_INPUT_HANDLE, GENERIC_READ);
        }
        out = options.discard_stdout
            ? open_file(L"NUL", GENERIC_WRITE, OPEN_EXISTING)
            : std_copy(STD_OUTPUT_HANDLE, GENERIC_WRITE);
        err = options.stderr_file
            ? open_file(options.stderr_file->wstring(), GENERIC_WRITE, CREATE_ALWAYS)
            : std_copy(STD_ERROR_HANDLE, GENERIC_WRITE);
        if (!std_handles.valid() || !std_handles.make_inheritable()) {
            return std::unexpected(win32_error());
        }
    }

    // Only these three handles go to the program, whatever else the app
    // has open that's inheritable.
    STARTUPINFOEXW startup { };
    startup.StartupInfo.cb = sizeof(startup);
    std::vector<char> attribute_buffer;
    if (redirect) {
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = std_handles.handles[0];
        startup.StartupInfo.hStdOutput = std_handles.handles[1];
        startup.StartupInfo.hStdError = std_handles.handles[2];
        SIZE_T size { 0 };
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attribute_buffer.resize(size);
        auto const list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_buffer.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) {
            return std::unexpected(win32_error());
        }
        startup.lpAttributeList = list;
        // Without the list, the program would inherit every inheritable
        // handle the app has: better not to start it.
        if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, std_handles.handles,
                sizeof(std_handles.handles), nullptr, nullptr)) {
            auto const error = win32_error();
            DeleteProcThreadAttributeList(list);
            return std::unexpected(error);
        }
    }

    // The program is looked for as Windows does without a path: next to
    // the app, in the system folders and on the PATH, with .exe added.
    auto                line = to_wide(command_line(command));
    PROCESS_INFORMATION process { };
    // No console window for a console program such as ffmpeg.
    auto const created = CreateProcessW(nullptr, line.data(), nullptr, nullptr, redirect ? TRUE : FALSE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process);
    auto const error = created ? LibCError { 0 } : win32_error();
    if (startup.lpAttributeList != nullptr) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
    }
    if (!created) {
        return std::unexpected(error);
    }
    CloseHandle(process.hThread);
    state->process = process.hProcess;
    return Child { std::move(state) };
}

Child::Child(std::unique_ptr<State> state)
    : m_state(std::move(state))
{
}

Child::Child(Child &&) noexcept = default;
Child &Child::operator=(Child &&) noexcept = default;

Child::~Child() = default;

bool Child::write(std::span<unsigned char const> bytes)
{
    while (!bytes.empty()) {
        DWORD      written { 0 };
        auto const chunk = static_cast<DWORD>(std::min<size_t>(bytes.size(), 1 << 30));
        if (!WriteFile(m_state->stdin_pipe, bytes.data(), chunk, &written, nullptr)) {
            return false;
        }
        bytes = bytes.subspan(written);
    }
    return true;
}

void Child::close_stdin()
{
    if (m_state->stdin_pipe != nullptr) {
        CloseHandle(m_state->stdin_pipe);
        m_state->stdin_pipe = nullptr;
    }
}

// The process handle keeps the process ID from being reused until the
// destructor closes it, so kill() can't get another process.
std::expected<int, LibCError> Child::wait()
{
    close_stdin();
    if (WaitForSingleObject(m_state->process, INFINITE) != WAIT_OBJECT_0) {
        return std::unexpected(win32_error());
    }
    DWORD exit_code { 0 };
    if (!GetExitCodeProcess(m_state->process, &exit_code)) {
        return std::unexpected(win32_error());
    }
    std::lock_guard lock { m_state->mutex };
    m_state->waited = true;
    return static_cast<int>(exit_code);
}

void Child::kill()
{
    std::lock_guard lock { m_state->mutex };
    if (!m_state->waited) {
        TerminateProcess(m_state->process, 1);
    }
}

}
