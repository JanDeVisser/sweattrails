/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include "Defer.h"
#include "FileBuffer.h"
#include "Logging.h"

namespace Obelix {

extern_logging_category(stringbuffer);

ErrorOr<void, SystemError> BufferLocator::check_existence(fs::path const& file_name)
{
    auto fh = ::open(file_name.c_str(), O_RDONLY);
    auto sg = ScopeGuard([fh]() { if (fh > 0) ::close(fh); });
    if (fh < 0) {
        switch (errno) {
        case ENOENT:
            return SystemError { ErrorCode::NoSuchFile, "File '{}' does not exist", file_name };
        default:
            return SystemError { ErrorCode::IOError, "Error opening file '{}'", file_name };
        }
    }

    struct stat sb;
    if (auto rc = fstat(fh, &sb); rc < 0)
        return SystemError { ErrorCode::IOError, "Error stat-ing file '{}'", file_name };
    if (S_ISDIR(sb.st_mode))
        return SystemError { ErrorCode::PathIsDirectory, "Path '{}' is a directory, not a file", file_name };
    return {};
}

ErrorOr<fs::path, SystemError> SimpleBufferLocator::locate(std::string const& file_name) const
{
    auto exists = check_existence(file_name);
    if (exists.is_error())
        return exists.error();
    return file_name;
}

FileBuffer::FileBuffer(fs::path path, char const* text, bool take_ownership)
    : StringBuffer(text, take_ownership)
    , m_path(std::move(path))
{
}

ErrorOr<std::shared_ptr<FileBuffer>, SystemError> FileBuffer::from_file(std::string const& file_name, BufferLocator* locator)
{
    static SimpleBufferLocator s_simple_locator;
    if (locator == nullptr)
        locator = &s_simple_locator;
    auto file_name_or_error = locator->locate(file_name);
    if (file_name_or_error.is_error())
        return file_name_or_error.error();
    auto full_file_name = file_name_or_error.value();

    auto fh = ::open(full_file_name.c_str(), O_RDONLY);
    auto file_closer = ScopeGuard([fh]() {
        if (fh > 0)
            close(fh);
    });
    if (fh < 0) {
        switch (errno) {
        case ENOENT:
            return SystemError { ErrorCode::NoSuchFile, "File '{}' does not exist", full_file_name };
        default:
            return SystemError { ErrorCode::IOError, "Error opening file '{}'", full_file_name };
        }
    }
    struct stat sb = { 0 };
    if (auto rc = fstat(fh, &sb); rc < 0)
        return SystemError { ErrorCode::IOError, "Error stat-ing file '{}'", full_file_name };
    if (S_ISDIR(sb.st_mode))
        return SystemError { ErrorCode::PathIsDirectory, "Path '{}' is a directory, not a file", full_file_name };

    auto size = sb.st_size;
    auto buffer = new char[size + 1];
    if (auto rc = ::read(fh, (void*)buffer, size); rc < size) {
        return SystemError { ErrorCode::IOError, "Error reading '{}'", full_file_name };
    }
    buffer[size] = '\0';
    return std::make_shared<FileBuffer>(full_file_name, buffer, true);
}

}
