/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Error.h>

namespace ST {

constexpr int ENOERROR = 0;

#ifdef IS_APPLE
#define ERRORCODES(S)                                                  \
    S(ENOERROR, "No error")                                            \
    S(EPERM, "Permission")                                             \
    S(ENOENT, "No such file or directory")                             \
    S(ESRCH, "No such process")                                        \
    S(EINTR, "Interrupted system call")                                \
    S(EIO, "Input/output error")                                       \
    S(ENXIO, "Device not configured")                                  \
    S(E2BIG, "Argument list too long")                                 \
    S(ENOEXEC, "Exec format error")                                    \
    S(EBADF, "Bad file descriptor")                                    \
    S(ECHILD, "No child processes")                                    \
    S(EDEADLK, "Resource deadlock avoided")                            \
    S(ENOMEM, "Cannot allocate memory")                                \
    S(EACCES, "Permission denied")                                     \
    S(EFAULT, "Bad address")                                           \
    S(ENOTBLK, "Block device required")                                \
    S(EBUSY, "Device / Resource busy")                                 \
    S(EEXIST, "File exists")                                           \
    S(EXDEV, "Cross-device link")                                      \
    S(ENODEV, "Operation not supported by device")                     \
    S(ENOTDIR, "Not a directory")                                      \
    S(EISDIR, "Is a directory")                                        \
    S(EINVAL, "Invalid argument")                                      \
    S(ENFILE, "Too many open files in system")                         \
    S(EMFILE, "Too many open files")                                   \
    S(ENOTTY, "Inappropriate ioctl for device")                        \
    S(ETXTBSY, "Text file busy")                                       \
    S(EFBIG, "File too large")                                         \
    S(ENOSPC, "No space left on device")                               \
    S(ESPIPE, "Illegal seek")                                          \
    S(EROFS, "Read-only file system")                                  \
    S(EMLINK, "Too many links")                                        \
    S(EPIPE, "Broken pipe")                                            \
    S(EDOM, "Numerical argument out of domain")                        \
    S(ERANGE, "Result too large")                                      \
    S(EAGAIN, "Resource temporarily unavailable")                      \
    S(EINPROGRESS, "Operation now in progress")                        \
    S(EALREADY, "Operation already in progress")                       \
    S(ENOTSOCK, "Socket operation on non-socket")                      \
    S(EDESTADDRREQ, "Destination address required")                    \
    S(EMSGSIZE, "Message too long")                                    \
    S(EPROTOTYPE, "Protocol wrong type for socket")                    \
    S(ENOPROTOOPT, "Protocol not available")                           \
    S(EPROTONOSUPPORT, "Protocol not supported")                       \
    S(ESOCKTNOSUPPORT, "Socket type not supported")                    \
    S(ENOTSUP, "Operation not supported")                              \
    S(EPFNOSUPPORT, "Protocol family not supported")                   \
    S(EAFNOSUPPORT, "Address family not supported by protocol family") \
    S(EADDRINUSE, "Address already in use")                            \
    S(EADDRNOTAVAIL, "Can't assign requested address")                 \
    S(ENETDOWN, "Network is down")                                     \
    S(ENETUNREACH, "Network is unreachable")                           \
    S(ENETRESET, "Network dropped connection on reset")                \
    S(ECONNABORTED, "Software caused connection abort")                \
    S(ECONNRESET, "Connection reset by peer")                          \
    S(ENOBUFS, "No buffer space available")                            \
    S(EISCONN, "Socket is already connected")                          \
    S(ENOTCONN, "Socket is not connected")                             \
    S(ESHUTDOWN, "Can't send after socket shutdown")                   \
    S(ETOOMANYREFS, "Too many references: can't splice")               \
    S(ETIMEDOUT, "Operation timed out")                                \
    S(ECONNREFUSED, "Connection refused")                              \
    S(ELOOP, "Too many levels of symbolic links")                      \
    S(ENAMETOOLONG, "File name too long")                              \
    S(EHOSTDOWN, "Host is down")                                       \
    S(EHOSTUNREACH, "No route to host")                                \
    S(ENOTEMPTY, "Directory not empty")                                \
    S(EPROCLIM, "Too many processes")                                  \
    S(EUSERS, "Too many users")                                        \
    S(EDQUOT, "Disc quota exceeded")                                   \
    S(ESTALE, "Stale NFS file handle")                                 \
    S(EREMOTE, "Too many levels of remote in path")                    \
    S(EBADRPC, "RPC struct is bad")                                    \
    S(ERPCMISMATCH, "RPC version wrong")                               \
    S(EPROGUNAVAIL, "RPC prog. not avail")                             \
    S(EPROGMISMATCH, "Program version wrong")                          \
    S(EPROCUNAVAIL, "Bad procedure for program")                       \
    S(ENOLCK, "No locks available")                                    \
    S(ENOSYS, "Function not implemented")                              \
    S(EFTYPE, "Inappropriate file type or format")                     \
    S(EAUTH, "Authentication error")                                   \
    S(ENEEDAUTH, "Need authenticator")                                 \
    S(EPWROFF, "Device power is off")                                  \
    S(EDEVERR, "Device error, e.g. paper out")                         \
    S(EOVERFLOW, "Value too large to be stored in data type")          \
    S(EBADEXEC, "Bad executable")                                      \
    S(EBADARCH, "Bad CPU type in executable")                          \
    S(ESHLIBVERS, "Shared library version mismatch")                   \
    S(EBADMACHO, "Malformed Macho file")                               \
    S(ECANCELED, "Operation canceled")                                 \
    S(EIDRM, "Identifier removed")                                     \
    S(ENOMSG, "No message of desired type")                            \
    S(EILSEQ, "Illegal byte sequence")                                 \
    S(ENOATTR, "Attribute not found")                                  \
    S(EBADMSG, "Bad message")                                          \
    S(EMULTIHOP, "Reserved")                                           \
    S(ENODATA, "No message available on STREAM")                       \
    S(ENOLINK, "Reserved")                                             \
    S(ENOSR, "No STREAM resources")                                    \
    S(ENOSTR, "Not a STREAM")                                          \
    S(EPROTO, "Protocol error")                                        \
    S(ETIME, "STREAM ioctl timeout")                                   \
    S(EOPNOTSUPP, "Operation not supported on socket")                 \
    S(ENOPOLICY, "No such policy registered")                          \
    S(ENOTRECOVERABLE, "State not recoverable")                        \
    S(EOWNERDEAD, "Previous owner died")                               \
    S(EQFULL, "Interface output char_queue is full")
#endif

#ifdef IS_LINUX

#define ERRORCODES(S)                                              \
    S(EPERM, "Operation not permitted")                            \
    S(ENOENT, "No such file or directory")                         \
    S(ESRCH, "No such process")                                    \
    S(EINTR, "Interrupted system call")                            \
    S(EIO, "I/O error")                                            \
    S(ENXIO, "No such device or address")                          \
    S(E2BIG, "Argument list too long")                             \
    S(ENOEXEC, "Exec format error")                                \
    S(EBADF, "Bad file number")                                    \
    S(ECHILD, "No child processes")                                \
    S(EAGAIN, "Try again")                                         \
    S(ENOMEM, "Out of memory")                                     \
    S(EACCES, "Permission denied")                                 \
    S(EFAULT, "Bad address")                                       \
    S(ENOTBLK, "Block device required")                            \
    S(EBUSY, "Device or resource busy")                            \
    S(EEXIST, "File exists")                                       \
    S(EXDEV, "Cross-device link")                                  \
    S(ENODEV, "No such device")                                    \
    S(ENOTDIR, "Not a directory")                                  \
    S(EISDIR, "Is a directory")                                    \
    S(EINVAL, "Invalid argument")                                  \
    S(ENFILE, "File table overflow")                               \
    S(EMFILE, "Too many open files")                               \
    S(ENOTTY, "Not a typewriter")                                  \
    S(ETXTBSY, "Text file busy")                                   \
    S(EFBIG, "File too large")                                     \
    S(ENOSPC, "No space left on device")                           \
    S(ESPIPE, "Illegal seek")                                      \
    S(EROFS, "Read-only file system")                              \
    S(EMLINK, "Too many links")                                    \
    S(EPIPE, "Broken pipe")                                        \
    S(EDOM, "Math argument out of domain of func")                 \
    S(ERANGE, "Math result not representable")                     \
    S(EDEADLK, "Resource deadlock would occur")                    \
    S(ENAMETOOLONG, "File name too long")                          \
    S(ENOLCK, "No record locks available")                         \
    S(ENOSYS, "Invalid system call number")                        \
    S(ENOTEMPTY, "Directory not empty")                            \
    S(ELOOP, "Too many symbolic links encountered")                \
    S(ENOMSG, "No message of desired type")                        \
    S(EIDRM, "Identifier removed")                                 \
    S(ECHRNG, "Channel number out of range")                       \
    S(EL2NSYNC, "Level 2 not synchronized")                        \
    S(EL3HLT, "Level 3 halted")                                    \
    S(EL3RST, "Level 3 reset")                                     \
    S(ELNRNG, "Link number out of range")                          \
    S(EUNATCH, "Protocol driver not attached")                     \
    S(ENOCSI, "No CSI structure available")                        \
    S(EL2HLT, "Level 2 halted")                                    \
    S(EBADE, "Invalid exchange")                                   \
    S(EBADR, "Invalid request descriptor")                         \
    S(EXFULL, "Exchange full")                                     \
    S(ENOANO, "No anode")                                          \
    S(EBADRQC, "Invalid request code")                             \
    S(EBADSLT, "Invalid slot")                                     \
    S(EBFONT, "Bad font file format")                              \
    S(ENOSTR, "Device not a stream")                               \
    S(ENODATA, "No data available")                                \
    S(ETIME, "Timer expired")                                      \
    S(ENOSR, "Out of streams resources")                           \
    S(ENONET, "Machine is not on the network")                     \
    S(ENOPKG, "Package not installed")                             \
    S(EREMOTE, "Object is remote")                                 \
    S(ENOLINK, "Link has been severed")                            \
    S(EADV, "Advertise error")                                     \
    S(ESRMNT, "Srmount error")                                     \
    S(ECOMM, "Communication error on send")                        \
    S(EPROTO, "Protocol error")                                    \
    S(EMULTIHOP, "Multihop attempted")                             \
    S(EDOTDOT, "RFS specific error")                               \
    S(EBADMSG, "Not a data message")                               \
    S(EOVERFLOW, "Value too large for defined data type")          \
    S(ENOTUNIQ, "Name not unique on network")                      \
    S(EBADFD, "File descriptor in bad state")                      \
    S(EREMCHG, "Remote address changed")                           \
    S(ELIBACC, "Can not access a needed shared library")           \
    S(ELIBBAD, "Accessing a corrupted shared library")             \
    S(ELIBSCN, ".lib section in a.out corrupted")                  \
    S(ELIBMAX, "Attempting to link in too many shared libraries")  \
    S(ELIBEXEC, "Cannot exec a shared library directly")           \
    S(EILSEQ, "Illegal byte sequence")                             \
    S(ERESTART, "Interrupted system call should be restarted")     \
    S(ESTRPIPE, "Streams pipe error")                              \
    S(EUSERS, "Too many users")                                    \
    S(ENOTSOCK, "Socket operation on non-socket")                  \
    S(EDESTADDRREQ, "Destination address required")                \
    S(EMSGSIZE, "Message too long")                                \
    S(EPROTOTYPE, "Protocol wrong type for socket")                \
    S(ENOPROTOOPT, "Protocol not available")                       \
    S(EPROTONOSUPPORT, "Protocol not supported")                   \
    S(ESOCKTNOSUPPORT, "Socket type not supported")                \
    S(EOPNOTSUPP, "Operation not supported on transport endpoint") \
    S(EPFNOSUPPORT, "Protocol family not supported")               \
    S(EAFNOSUPPORT, "Address family not supported by protocol")    \
    S(EADDRINUSE, "Address already in use")                        \
    S(EADDRNOTAVAIL, "Cannot assign requested address")            \
    S(ENETDOWN, "Network is down")                                 \
    S(ENETUNREACH, "Network is unreachable")                       \
    S(ENETRESET, "Network dropped connection because of reset")    \
    S(ECONNABORTED, "Software caused connection abort")            \
    S(ECONNRESET, "Connection reset by peer")                      \
    S(ENOBUFS, "No buffer space available")                        \
    S(EISCONN, "Transport endpoint is already connected")          \
    S(ENOTCONN, "Transport endpoint is not connected")             \
    S(ESHUTDOWN, "Cannot send after transport endpoint shutdown")  \
    S(ETOOMANYREFS, "Too many references: cannot splice")          \
    S(ETIMEDOUT, "Connection timed out")                           \
    S(ECONNREFUSED, "Connection refused")                          \
    S(EHOSTDOWN, "Host is down")                                   \
    S(EHOSTUNREACH, "No route to host")                            \
    S(EALREADY, "Operation already in progress")                   \
    S(EINPROGRESS, "Operation now in progress")                    \
    S(ESTALE, "Stale file handle")                                 \
    S(EUCLEAN, "Structure needs cleaning")                         \
    S(ENOTNAM, "Not a XENIX named type file")                      \
    S(ENAVAIL, "No XENIX semaphores available")                    \
    S(EISNAM, "Is a named type file")                              \
    S(EREMOTEIO, "Remote I/O error")                               \
    S(EDQUOT, "Quota exceeded")                                    \
    S(ENOMEDIUM, "No medium found")                                \
    S(EMEDIUMTYPE, "Wrong medium type")                            \
    S(ECANCELED, "Operation Canceled")                             \
    S(ENOKEY, "Required key not available")                        \
    S(EKEYEXPIRED, "Key has expired")                              \
    S(EKEYREVOKED, "Key has been revoked")                         \
    S(EKEYREJECTED, "Key was rejected by service")                 \
    S(EOWNERDEAD, "Owner died")                                    \
    S(ENOTRECOVERABLE, "State not recoverable")                    \
    S(ERFKILL, "Operation not possible due to RF-kill")            \
    S(EHWPOISON, "Memory page has hardware error")

#endif

LibCError::LibCError(int err) noexcept
{
    err_no = err;
    switch (err) {
#undef S
#define S(N, D)          \
    case N:              \
        code = #N;       \
        description = D; \
        break;
        ERRORCODES(S)
#undef S
        break;
    case ECUSTOM:
        code = "ECUSTOM";
        description = "Custom error message";
    default:
        code = "UNKNOWN";
        description = "Unknown error";
        break;
    }
}

}
