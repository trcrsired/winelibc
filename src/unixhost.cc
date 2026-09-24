/*
libwineunix.so — the unix side of wineunix.dll. Compiled as a normal native shared
library (clang/gcc, -fPIC -shared) with -DWINE_UNIX_LIB; no wine toolchain needed.

Wine's loader dlopen's this .so and dlsym's "__wine_unix_call_funcs" (and
"__wine_unix_call_wow64_funcs" for a 32-bit PE on a 64-bit host) when the PE side asks
for it through NtQueryVirtualMemory(GetCurrentProcess(), &name, MemoryWineLoadUnixLibByName, ...).
See dlls/ntdll/unix/virtual.c in the wine source.
*/

#define WINE_UNIX_LIB 1
#include "__wine_unix_abi.h"
#include <__wine_unix/__wine_unix_errno.h>
#include <__wine_unix/__wine_unix_fcntl.h>

#include <cstdint>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

/*
ntdll.so unix exports — wineserver-backed fd <-> HANDLE conversion. Resolved
once by __wine_unix_lib_init — no DT_NEEDED and no wine libdir path needed.
The handles they make live in the PE process's own handle table — unixlibs
run inside it.

ntdll.so is already mapped (it dlopen'd us) but RTLD_LOCAL, so RTLD_DEFAULT
cannot see its exports. dlopen by soname with RTLD_NOLOAD returns the handle
of the already-loaded copy without pulling a second one.
*/
using wine_server_fd_to_handle_t = unsigned int(int, unsigned int, unsigned int, void **) noexcept;
using wine_server_handle_to_fd_t = unsigned int(void *, unsigned int, int *, unsigned int *) noexcept;
using rtl_get_current_peb_t = void *() noexcept;
using nt_compare_objects_t = unsigned int(void *, void *) noexcept;

static wine_server_fd_to_handle_t *wine_server_fd_to_handle_p;
static wine_server_handle_to_fd_t *wine_server_handle_to_fd_p;
static rtl_get_current_peb_t *rtl_get_current_peb_p;
static nt_compare_objects_t *nt_compare_objects_p;

/* 64-bit PEB/RTL_USER_PROCESS_PARAMETERS headers — unix side is always 64-bit */
struct unix_rtl_user_process_parameters
{
	uint32_t MaximumLength;
	uint32_t Length;
	uint32_t Flags;
	uint32_t DebugFlags;
	void *ConsoleHandle;
	uint32_t ConsoleFlags;
	void *StandardInput;
	void *StandardOutput;
	void *StandardError;
};

struct unix_peb
{
	uint8_t InheritedAddressSpace;
	uint8_t ReadImageFileExecOptions;
	uint8_t BeingDebugged;
	uint8_t SpareBool;
	void *Mutant;
	void *ImageBaseAddress;
	void *Ldr;
	unix_rtl_user_process_parameters *ProcessParameters;
};

namespace __wine_unix
{
#if defined(__linux__) && defined(__x86_64__)
/* linux x86-64 errno values already match the ones in __wine_unix_errno.h */
inline __wine_unix_status_t host_errno_to_wine_errno(int val) noexcept
{
	return static_cast<__wine_unix_status_t>(val);
}
#else
__wine_unix_status_t host_errno_to_wine_errno(int) noexcept;
#endif

namespace
{

/* win32 access bits used by the wineserver calls */
constexpr unsigned int wine_generic_read{0x80000000u};
constexpr unsigned int wine_generic_write{0x40000000u};
constexpr unsigned int wine_synchronize{0x00100000u};
constexpr unsigned int wine_obj_inherit{0x00000002u};

/* handle rights the new handle should hold, mirroring the fd's O_ACCMODE */
inline unsigned int unix_fd_access(int unix_fd) noexcept
{
	int const fl{::fcntl(unix_fd, F_GETFL)};
	unsigned int access{wine_synchronize};
	if (fl < 0 || (fl & O_ACCMODE) != O_WRONLY)
	{
		access |= wine_generic_read;
	}
	if (fl < 0 || (fl & O_ACCMODE) != O_RDONLY)
	{
		access |= wine_generic_write;
	}
	return access;
}

inline __wine_unix_status_t host_fd_to_unix_fd(__wine_host_fd_t host_fd, int &unix_fd) noexcept
{
	unix_fd = -1;
	if (host_fd == 0)
	{
		return __WINE_UNIX_ERRNO_EBADF;
	}
	/*
	host_fd encodes unix_fd + 1; decoding negative unix fds (AT_FDCWD and
	friends) wraps, so decode first and only then validate the result.
	*/
	unix_fd = static_cast<int>(host_fd - 1);
	if (unix_fd < 0 && unix_fd != AT_FDCWD)
	{
		unix_fd = -1;
		return __WINE_UNIX_ERRNO_EBADF;
	}
	return __WINE_UNIX_ERRNO_SUCCESS;
}

inline __wine_host_fd_t unix_fd_to_host_fd(int unix_fd) noexcept
{
	if (unix_fd < 0)
	{
		return 0;
	}
	return static_cast<__wine_host_fd_t>(unix_fd) + 1;
}

struct c_path_malloc_guard
{
	__wine_errno_t host_errno{__WINE_UNIX_ERRNO_EINVAL};
	char *filename_c_str{};
	c_path_malloc_guard() noexcept = default;
	c_path_malloc_guard(__wine_errno_t err, char *ptr) noexcept
		: host_errno{err}, filename_c_str{ptr}
	{}
	c_path_malloc_guard(c_path_malloc_guard const &) = delete;
	c_path_malloc_guard &operator=(c_path_malloc_guard const &) = delete;
	~c_path_malloc_guard()
	{
		free(filename_c_str);
	}
};

/*
no NUL may appear inside [filename, filename + filenamelen) — an interior NUL
would silently truncate the path for the C api. the terminated copy is
allocated here on the unix side; a future sized-path host api may skip it.
*/
inline c_path_malloc_guard c_path_common(char const *filename, size_t filenamelen) noexcept
{
	if (filenamelen == SIZE_MAX)
	{
		return {};
	}
	if (strnlen(filename, filenamelen) != filenamelen)
	{
		return {};
	}
	char *newmem = static_cast<char *>(malloc(filenamelen + 1));
	if (newmem == nullptr)
	{
		return {__WINE_UNIX_ERRNO_ENOMEM, nullptr};
	}
	if (filenamelen)
	{
		memcpy(newmem, filename, filenamelen);
	}
	newmem[filenamelen] = 0;
	return {__WINE_UNIX_ERRNO_SUCCESS, newmem};
}

inline __wine_unix_status_t readwritev_result_common_split(ssize_t ret, __wine_unix_iovec_t const *iovs, size_t iovsize,
														   size_t &baseindex, size_t &index) noexcept
{
	if (ret == -1)
	{
		return host_errno_to_wine_errno(errno);
	}
	size_t uret{static_cast<size_t>(ret)};
	baseindex = 0;
	index = 0;
	if (iovsize)
	{
		auto *const iovsed{iovs + iovsize};
		auto *i{iovs};
		size_t lastn{uret};
		for (; i != iovsed; ++i)
		{
			size_t const ilen{i->iov_len};
			if (lastn < ilen)
			{
				break;
			}
			lastn -= ilen;
		}
		baseindex = static_cast<size_t>(i - iovs);
		index = lastn;
	}
	return __WINE_UNIX_ERRNO_SUCCESS;
}

inline __wine_unix_iovec_t const *iovs_from_params(auto const &params) noexcept
{
	return reinterpret_cast<__wine_unix_iovec_t const *>(reinterpret_cast<::std::uintptr_t>(params.iovs));
}

template <typename Fn>
inline __wine_unix_status_t readwritev_common(__wine_unix_readwritev_params_t *params, Fn fn) noexcept
{
	int unix_fd{-1};
	if (auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)}; errcode)
	{
		return errcode;
	}
	size_t iovsize{static_cast<size_t>(params->iovsize)};
	constexpr int mxval{
#ifdef IOV_MAX
		IOV_MAX
#else
		INT_MAX
#endif
	};
	if (mxval < iovsize)
	{
		iovsize = mxval;
	}
	__wine_unix_iovec_t const *const iovs{iovs_from_params(*params)};
	auto ret = fn(unix_fd, reinterpret_cast<struct iovec const *>(iovs), static_cast<int>(iovsize));
	size_t baseindex{};
	size_t index{};
	auto const errcode{readwritev_result_common_split(ret, iovs, iovsize, baseindex, index)};
	params->baseindex = static_cast<decltype(params->baseindex)>(baseindex);
	params->index = static_cast<decltype(params->index)>(index);
	return errcode;
}

template <typename Fn>
inline __wine_unix_status_t preadwritev_common(__wine_unix_preadwritev_params_t *params, Fn fn) noexcept
{
	if constexpr (sizeof(off_t) < sizeof(__wine_off_t))
	{
		constexpr off_t off_min{::std::numeric_limits<off_t>::min()};
		constexpr off_t off_max{::std::numeric_limits<off_t>::max()};
		if (params->offset < off_min || off_max < params->offset)
		{
			return __WINE_UNIX_ERRNO_EOVERFLOW;
		}
	}
	int unix_fd{-1};
	if (auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)}; errcode)
	{
		return errcode;
	}
	size_t iovsize{static_cast<size_t>(params->iovsize)};
	constexpr int mxval{
#ifdef IOV_MAX
		IOV_MAX
#else
		INT_MAX
#endif
	};
	if (mxval < iovsize)
	{
		iovsize = mxval;
	}
	__wine_unix_iovec_t const *const iovs{iovs_from_params(*params)};
	auto ret = fn(unix_fd, reinterpret_cast<struct iovec const *>(iovs), static_cast<int>(iovsize));
	size_t baseindex{};
	size_t index{};
	auto const errcode{readwritev_result_common_split(ret, iovs, iovsize, baseindex, index)};
	params->baseindex = static_cast<decltype(params->baseindex)>(baseindex);
	params->index = static_cast<decltype(params->index)>(index);
	return errcode;
}

static __wine_unix_status_t unix_host_fd_to_unix_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_host_fd_to_unix_fd_params_t *>(args)};
	int unix_fd{-1};
	auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)};
	params->unix_fd = unix_fd;
	return errcode;
}

static __wine_unix_status_t unix_unix_fd_to_host_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_unix_fd_to_host_fd_params_t *>(args)};
	params->host_fd = unix_fd_to_host_fd(static_cast<int>(params->unix_fd));
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t unix_host_fd_to_nt_handle(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_host_fd_to_nt_handle_params *>(args)};
	int unix_fd{};
	if (auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)}; errcode)
	{
		return errcode;
	}
	auto *const fd_to_handle{wine_server_fd_to_handle_p};
	if (fd_to_handle == nullptr)
	{
		return __WINE_UNIX_ERRNO_ENOSYS;
	}
	void *handle{};
	if (fd_to_handle(unix_fd, unix_fd_access(unix_fd), wine_obj_inherit, &handle))
	{
		return __WINE_UNIX_ERRNO_EBADF;
	}
	/* the wineserver holds its own fd via SCM_RIGHTS — close ours: the conversion consumed host_fd */
	::close(unix_fd);
	params->handle = static_cast<ptrdiff_t>(reinterpret_cast<uintptr_t>(handle));
	return __WINE_UNIX_ERRNO_SUCCESS;
}

/*
If handle denotes one of the process's standard objects, return which slot
(0/1/2), else -1. Console handles have no wineserver-visible fd — same
fallback as wine's own spawn_process, which maps them to unix fds 0/1/2.
NtCompareObjects catches handles duplicated onto the same console object.
Only reached after wine_server_handle_to_fd fails, so a std slot repointed
at a real file (SetStdHandle) still gets a proper fd for that object.
*/
static int unix_std_handle_which(void *handle) noexcept
{
	if (handle == nullptr || rtl_get_current_peb_p == nullptr)
	{
		return -1;
	}
	auto const *const pparam{
		static_cast<unix_peb const *>(rtl_get_current_peb_p())->ProcessParameters};
	void *const stds[3]{pparam->StandardInput, pparam->StandardOutput, pparam->StandardError};
	for (int which{}; which != 3; ++which)
	{
		if (handle == stds[which])
		{
			return which;
		}
	}
	if (nt_compare_objects_p != nullptr)
	{
		for (int which{}; which != 3; ++which)
		{
			if (stds[which] != nullptr && !nt_compare_objects_p(handle, stds[which]))
			{
				return which;
			}
		}
	}
	return -1;
}

static __wine_unix_status_t unix_nt_handle_to_host_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_nt_handle_to_host_fd_params *>(args)};
	auto *const handle_to_fd{wine_server_handle_to_fd_p};
	if (handle_to_fd == nullptr)
	{
		return __WINE_UNIX_ERRNO_ENOSYS;
	}
	int unix_fd{};
	/* access 0: convert whatever the handle grants; the fd keeps the object's real mode */
	if (handle_to_fd(reinterpret_cast<void *>(static_cast<uintptr_t>(params->handle)),
					 0, &unix_fd, nullptr))
	{
		int const which{unix_std_handle_which(reinterpret_cast<void *>(static_cast<uintptr_t>(params->handle)))};
		if (which < 0)
		{
			return __WINE_UNIX_ERRNO_EBADF;
		}
		params->host_fd = unix_fd_to_host_fd(which);
		return __WINE_UNIX_ERRNO_SUCCESS;
	}
	params->host_fd = unix_fd_to_host_fd(unix_fd);
	return __WINE_UNIX_ERRNO_SUCCESS;
}

/*
__WINE_UNIX_O_* is the wire encoding — fixed x86-64 linux values, see
__wine_unix_fcntl.h. Hosts renumber some of the bits (aarch64 moves
O_DIRECT, O_LARGEFILE, O_DIRECTORY, O_NOFOLLOW and O_TMPFILE), so decode
bit by bit; composites (__WINE_UNIX_O_SYNC, __WINE_UNIX_O_TMPFILE) fall
out of the per-bit mapping. Bits the host lacks fail with EINVAL rather
than silently dropping.
*/
inline __wine_unix_status_t wine_flags_to_host_open_flags(::std::uint_least64_t flags, int &host_flags) noexcept
{
	int fl{};
	switch (flags & __WINE_UNIX_O_ACCMODE)
	{
	case __WINE_UNIX_O_RDONLY:
		fl = O_RDONLY;
		break;
	case __WINE_UNIX_O_WRONLY:
		fl = O_WRONLY;
		break;
	case __WINE_UNIX_O_RDWR:
		fl = O_RDWR;
		break;
	default:
		return __WINE_UNIX_ERRNO_EINVAL;
	}
	for (::std::uint_least64_t rest{flags & ~static_cast<::std::uint_least64_t>(__WINE_UNIX_O_ACCMODE)}; rest != 0;
		 rest &= rest - 1)
	{
		switch (rest & (~rest + 1))
		{
		case __WINE_UNIX_O_CREAT:
			fl |= O_CREAT;
			break;
		case __WINE_UNIX_O_EXCL:
			fl |= O_EXCL;
			break;
		case __WINE_UNIX_O_NOCTTY:
			fl |= O_NOCTTY;
			break;
		case __WINE_UNIX_O_TRUNC:
			fl |= O_TRUNC;
			break;
		case __WINE_UNIX_O_APPEND:
			fl |= O_APPEND;
			break;
		case __WINE_UNIX_O_NONBLOCK:
			fl |= O_NONBLOCK;
			break;
		case __WINE_UNIX_O_DSYNC:
#ifdef O_DSYNC
			fl |= O_DSYNC;
			break;
#else
			return __WINE_UNIX_ERRNO_EINVAL;
#endif
		case __WINE_UNIX_O_DIRECT:
#ifdef O_DIRECT
			fl |= O_DIRECT;
			break;
#else
			return __WINE_UNIX_ERRNO_EINVAL;
#endif
		case __WINE_UNIX_O_LARGEFILE:
#ifdef O_LARGEFILE
			/* a no-op hint on 64-bit hosts; fine to drop where undefined */
			fl |= O_LARGEFILE;
#endif
			break;
		case __WINE_UNIX_O_DIRECTORY:
#ifdef O_DIRECTORY
			fl |= O_DIRECTORY;
			break;
#else
			return __WINE_UNIX_ERRNO_EINVAL;
#endif
		case __WINE_UNIX_O_NOFOLLOW:
#ifdef O_NOFOLLOW
			fl |= O_NOFOLLOW;
			break;
#else
			return __WINE_UNIX_ERRNO_EINVAL;
#endif
		case __WINE_UNIX_O_NOATIME:
#ifdef O_NOATIME
			fl |= O_NOATIME;
			break;
#else
			return __WINE_UNIX_ERRNO_EINVAL;
#endif
		case __WINE_UNIX_O_CLOEXEC:
#ifdef O_CLOEXEC
			fl |= O_CLOEXEC;
			break;
#else
			return __WINE_UNIX_ERRNO_EINVAL;
#endif
		case __WINE_UNIX_O_SYNC & ~__WINE_UNIX_O_DSYNC: /* the __O_SYNC bit */
#if defined(O_SYNC) && defined(O_DSYNC)
			fl |= O_SYNC & ~O_DSYNC;
			break;
#else
			return __WINE_UNIX_ERRNO_EINVAL;
#endif
		case __WINE_UNIX_O_PATH:
#ifdef O_PATH
			fl |= O_PATH;
			break;
#else
			return __WINE_UNIX_ERRNO_EINVAL;
#endif
		case __WINE_UNIX_O_TMPFILE & ~__WINE_UNIX_O_DIRECTORY: /* the __O_TMPFILE bit */
#if defined(O_TMPFILE) && defined(O_DIRECTORY)
			fl |= O_TMPFILE & ~O_DIRECTORY;
			break;
#else
			return __WINE_UNIX_ERRNO_EINVAL;
#endif
		default:
			return __WINE_UNIX_ERRNO_EINVAL;
		}
	}
	host_flags = fl;
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t unix_open_common(int dirfd, char const *filename, size_t filenamelen,
											 __wine_host_flags_t flags, __wine_host_mode_t mode,
											 __wine_host_fd_t &out_host_fd) noexcept
{
	auto pathret{c_path_common(filename, filenamelen)};
	if (pathret.host_errno)
	{
		return pathret.host_errno;
	}
	int host_flags{};
	if (auto const errcode{wine_flags_to_host_open_flags(flags, host_flags)}; errcode)
	{
		return errcode;
	}
	int const unix_fd{::openat(dirfd, pathret.filename_c_str, host_flags, static_cast<mode_t>(mode))};
	out_host_fd = unix_fd_to_host_fd(unix_fd);
	if (unix_fd == -1)
	{
		return host_errno_to_wine_errno(errno);
	}
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t unix_openat(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_openat_params_t *>(args)};
	int dirfd{-1};
	if (params->host_dirfd == 0)
	{
		dirfd = AT_FDCWD;
	}
	else if (auto const errcode{host_fd_to_unix_fd(params->host_dirfd, dirfd)}; errcode)
	{
		return errcode;
	}
	return unix_open_common(dirfd, params->filename, static_cast<size_t>(params->filenamelen), params->flags,
							params->mode, params->host_fd);
}

/* plain open(): relative paths resolve against cwd — saves the caller the at_fdcwd round trip */
static __wine_unix_status_t unix_open(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_open_params_t *>(args)};
	return unix_open_common(AT_FDCWD, params->filename, static_cast<size_t>(params->filenamelen), params->flags,
							params->mode, params->host_fd);
}

/*
this handler only ever runs when the unix backend answered the unixcall —
the params layouts are identical for both ABIs, so the same function serves
__wine_unix_call_funcs and __wine_unix_call_wow64_funcs.
*/
static __wine_unix_status_t unix_is_unix(void *args) noexcept
{
	static_cast<__wine_unix_is_unix_params *>(args)->is_unix = 1;
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t unix_close(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_close_params_t *>(args)};
	int unix_fd{-1};
	if (auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)}; errcode)
	{
		return errcode;
	}
	if (::close(unix_fd) == -1)
	{
		return host_errno_to_wine_errno(errno);
	}
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t unix_writev(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_readwritev_params_t *>(args)};
	return readwritev_common(params, [](int fd, struct iovec const *iov, int iovcnt) noexcept {
		return ::writev(fd, iov, iovcnt);
	});
}

static __wine_unix_status_t unix_readv(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_readwritev_params_t *>(args)};
	return readwritev_common(params, [](int fd, struct iovec const *iov, int iovcnt) noexcept {
		return ::readv(fd, iov, iovcnt);
	});
}

static __wine_unix_status_t unix_pwritev(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_preadwritev_params_t *>(args)};
	return preadwritev_common(params, [params](int fd, struct iovec const *iov, int iovcnt) noexcept {
		return ::pwritev(fd, iov, iovcnt, static_cast<off_t>(params->offset));
	});
}

static __wine_unix_status_t unix_preadv(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_preadwritev_params_t *>(args)};
	return preadwritev_common(params, [params](int fd, struct iovec const *iov, int iovcnt) noexcept {
		return ::preadv(fd, iov, iovcnt, static_cast<off_t>(params->offset));
	});
}

static __wine_unix_status_t unix_write(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_readwrite_params_t *>(args)};
	int unix_fd{-1};
	if (auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)}; errcode)
	{
		return errcode;
	}
	auto ret{::write(unix_fd, params->buf, static_cast<size_t>(params->len))};
	if (ret == -1)
	{
		return host_errno_to_wine_errno(errno);
	}
	params->total = static_cast<decltype(params->total)>(ret);
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t unix_read(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_readwrite_params_t *>(args)};
	int unix_fd{-1};
	if (auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)}; errcode)
	{
		return errcode;
	}
	auto ret{::read(unix_fd, params->buf, static_cast<size_t>(params->len))};
	if (ret == -1)
	{
		return host_errno_to_wine_errno(errno);
	}
	params->total = static_cast<decltype(params->total)>(ret);
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t unix_get_std_host_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_get_std_host_fd_params_t *>(args)};
	if (params->which < 0 || 2 < params->which)
	{
		return __WINE_UNIX_ERRNO_EINVAL;
	}
	params->host_fd = unix_fd_to_host_fd(params->which);
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t unix_at_fdcwd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_at_fdcwd_params_t *>(args)};
	/* host_fd encodes unix_fd + 1; AT_FDCWD keeps that encoding */
	params->host_fd = static_cast<__wine_host_fd_t>(AT_FDCWD) + 1;
	return __WINE_UNIX_ERRNO_SUCCESS;
}

#if INTPTR_MAX >= INT64_MAX
/*
wow64 (32-bit PE on a 64-bit host) wrappers. args points at a *_params32 struct laid
out by a 32-bit compiler; we widen pointers/sizes into a 64-bit params struct, call the
64-bit implementation, and write the outputs back.
*/

static __wine_unix_status_t wow64_unix_host_fd_to_unix_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_host_fd_to_unix_fd_params32 *>(args)};
	int unix_fd{-1};
	auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)};
	params->unix_fd = unix_fd;
	return errcode;
}

static __wine_unix_status_t wow64_unix_unix_fd_to_host_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_unix_fd_to_host_fd_params32 *>(args)};
	params->host_fd = static_cast<__wine_unix_ptr32_t>(unix_fd_to_host_fd(params->unix_fd));
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t wow64_unix_host_fd_to_nt_handle(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_host_fd_to_nt_handle_params32 *>(args)};
	int unix_fd{};
	if (auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)}; errcode)
	{
		return errcode;
	}
	auto *const fd_to_handle{wine_server_fd_to_handle_p};
	if (fd_to_handle == nullptr)
	{
		return __WINE_UNIX_ERRNO_ENOSYS;
	}
	void *handle{};
	if (fd_to_handle(unix_fd, unix_fd_access(unix_fd), wine_obj_inherit, &handle))
	{
		return __WINE_UNIX_ERRNO_EBADF;
	}
	/* the wineserver holds its own fd via SCM_RIGHTS — close ours: the conversion consumed host_fd */
	::close(unix_fd);
	params->handle = static_cast<int32_t>(reinterpret_cast<uintptr_t>(handle));
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t wow64_unix_nt_handle_to_host_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_nt_handle_to_host_fd_params32 *>(args)};
	auto *const handle_to_fd{wine_server_handle_to_fd_p};
	if (handle_to_fd == nullptr)
	{
		return __WINE_UNIX_ERRNO_ENOSYS;
	}
	int unix_fd{};
	auto *const handle{reinterpret_cast<void *>(
		static_cast<uintptr_t>(static_cast<::std::uint_least32_t>(params->handle)))};
	if (handle_to_fd(handle, 0, &unix_fd, nullptr))
	{
		int const which{unix_std_handle_which(handle)};
		if (which < 0)
		{
			return __WINE_UNIX_ERRNO_EBADF;
		}
		params->host_fd = static_cast<__wine_unix_ptr32_t>(unix_fd_to_host_fd(which));
		return __WINE_UNIX_ERRNO_SUCCESS;
	}
	params->host_fd = static_cast<__wine_unix_ptr32_t>(unix_fd_to_host_fd(unix_fd));
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t wow64_unix_openat(void *args) noexcept
{
	auto *params32{static_cast<__wine_unix_openat_params32 *>(args)};
	__wine_unix_openat_params params{};
	params.host_dirfd = params32->host_dirfd;
	params.filename = reinterpret_cast<char const *>(static_cast<::std::uintptr_t>(params32->filename));
	params.filenamelen = params32->filenamelen;
	params.flags = params32->flags;
	params.mode = params32->mode;
	auto const errcode{unix_openat(&params)};
	params32->host_fd = static_cast<__wine_unix_ptr32_t>(params.host_fd);
	return errcode;
}

static __wine_unix_status_t wow64_unix_close(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_close_params32 *>(args)};
	int unix_fd{-1};
	if (auto const errcode{host_fd_to_unix_fd(params->host_fd, unix_fd)}; errcode)
	{
		return errcode;
	}
	if (::close(unix_fd) == -1)
	{
		return host_errno_to_wine_errno(errno);
	}
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t wow64_readwritev_common(void *args, bool write) noexcept
{
	auto *params32{static_cast<__wine_unix_readwritev_params32 *>(args)};
	__wine_unix_readwritev_params params{};
	params.host_fd = params32->host_fd;
	size_t const iovsize{params32->iovsize};
	struct iovec *iovs{nullptr};
	if (iovsize)
	{
		iovs = static_cast<struct iovec *>(malloc(sizeof(struct iovec) * iovsize));
		if (iovs == nullptr)
		{
			return __WINE_UNIX_ERRNO_ENOMEM;
		}
		auto const *src{reinterpret_cast<uint32_t const *>(static_cast<::std::uintptr_t>(params32->iovs))};
		for (size_t i{}; i != iovsize; ++i)
		{
			iovs[i].iov_base = reinterpret_cast<void *>(static_cast<::std::uintptr_t>(src[2 * i]));
			iovs[i].iov_len = src[2 * i + 1];
		}
	}
	params.iovs = reinterpret_cast<__wine_unix_iovec_t const *>(iovs);
	params.iovsize = iovsize;
	auto const errcode{write ? unix_writev(&params) : unix_readv(&params)};
	params32->baseindex = static_cast<uint32_t>(params.baseindex);
	params32->index = static_cast<uint32_t>(params.index);
	free(iovs);
	return errcode;
}

static __wine_unix_status_t wow64_unix_writev(void *args) noexcept
{
	return wow64_readwritev_common(args, true);
}

static __wine_unix_status_t wow64_unix_readv(void *args) noexcept
{
	return wow64_readwritev_common(args, false);
}

static __wine_unix_status_t wow64_preadwritev_common(void *args, bool write) noexcept
{
	auto *params32{static_cast<__wine_unix_preadwritev_params32 *>(args)};
	__wine_unix_preadwritev_params params{};
	params.host_fd = params32->host_fd;
	params.offset = params32->offset;
	size_t const iovsize{params32->iovsize};
	struct iovec *iovs{nullptr};
	if (iovsize)
	{
		iovs = static_cast<struct iovec *>(malloc(sizeof(struct iovec) * iovsize));
		if (iovs == nullptr)
		{
			return __WINE_UNIX_ERRNO_ENOMEM;
		}
		auto const *src{reinterpret_cast<uint32_t const *>(static_cast<::std::uintptr_t>(params32->iovs))};
		for (size_t i{}; i != iovsize; ++i)
		{
			iovs[i].iov_base = reinterpret_cast<void *>(static_cast<::std::uintptr_t>(src[2 * i]));
			iovs[i].iov_len = src[2 * i + 1];
		}
	}
	params.iovs = reinterpret_cast<__wine_unix_iovec_t const *>(iovs);
	params.iovsize = iovsize;
	auto const errcode{write ? unix_pwritev(&params) : unix_preadv(&params)};
	params32->baseindex = static_cast<uint32_t>(params.baseindex);
	params32->index = static_cast<uint32_t>(params.index);
	free(iovs);
	return errcode;
}

static __wine_unix_status_t wow64_unix_pwritev(void *args) noexcept
{
	return wow64_preadwritev_common(args, true);
}

static __wine_unix_status_t wow64_unix_preadv(void *args) noexcept
{
	return wow64_preadwritev_common(args, false);
}

static __wine_unix_status_t wow64_readwrite_common(void *args, bool write) noexcept
{
	auto *params32{static_cast<__wine_unix_readwrite_params32 *>(args)};
	__wine_unix_readwrite_params params{};
	params.host_fd = params32->host_fd;
	params.buf = reinterpret_cast<void *>(static_cast<::std::uintptr_t>(params32->buf));
	params.len = params32->len;
	auto const errcode{(write ? unix_write : unix_read)(&params)};
	params32->total = static_cast<uint32_t>(params.total);
	return errcode;
}

static __wine_unix_status_t wow64_unix_write(void *args) noexcept
{
	return wow64_readwrite_common(args, true);
}

static __wine_unix_status_t wow64_unix_read(void *args) noexcept
{
	return wow64_readwrite_common(args, false);
}

static __wine_unix_status_t wow64_unix_get_std_host_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_get_std_host_fd_params32 *>(args)};
	if (params->which < 0 || 2 < params->which)
	{
		return __WINE_UNIX_ERRNO_EINVAL;
	}
	params->host_fd = static_cast<__wine_unix_ptr32_t>(unix_fd_to_host_fd(params->which));
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t wow64_unix_at_fdcwd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_at_fdcwd_params32 *>(args)};
	params->host_fd = static_cast<__wine_unix_ptr32_t>(static_cast<uint32_t>(AT_FDCWD) + 1);
	return __WINE_UNIX_ERRNO_SUCCESS;
}

static __wine_unix_status_t wow64_unix_open(void *args) noexcept
{
	auto *params32{static_cast<__wine_unix_open_params32 *>(args)};
	__wine_unix_open_params params{};
	params.filename = reinterpret_cast<char const *>(static_cast<::std::uintptr_t>(params32->filename));
	params.filenamelen = params32->filenamelen;
	params.flags = params32->flags;
	params.mode = params32->mode;
	auto const errcode{unix_open(&params)};
	params32->host_fd = static_cast<__wine_unix_ptr32_t>(params.host_fd);
	return errcode;
}

#endif // INTPTR_MAX >= INT64_MAX

} // namespace

static_assert(sizeof(__wine_unix_iovec_t) == sizeof(struct iovec),
			  "__wine_unix_iovec_t must match struct iovec layout");

/* every decoded bit must have mapped onto a host value */
static_assert((__WINE_UNIX_O_SYNC & ~__WINE_UNIX_O_DSYNC) != 0 && (__WINE_UNIX_O_TMPFILE & ~__WINE_UNIX_O_DIRECTORY) != 0,
			  "composite __WINE_UNIX_O_* flags must decompose into single bits");

} // namespace __wine_unix

extern "C"
{
	__wine_unixlib_entry_t const __wine_unix_call_funcs[] = {
		::__wine_unix::unix_host_fd_to_unix_fd,
		::__wine_unix::unix_unix_fd_to_host_fd,
		::__wine_unix::unix_host_fd_to_nt_handle,
		::__wine_unix::unix_nt_handle_to_host_fd,
		::__wine_unix::unix_openat,
		::__wine_unix::unix_close,
		::__wine_unix::unix_writev,
		::__wine_unix::unix_readv,
		::__wine_unix::unix_pwritev,
		::__wine_unix::unix_preadv,
		::__wine_unix::unix_write,
		::__wine_unix::unix_read,
		::__wine_unix::unix_get_std_host_fd,
		::__wine_unix::unix_at_fdcwd,
		::__wine_unix::unix_open,
		::__wine_unix::unix_is_unix,
	};

#if INTPTR_MAX >= INT64_MAX
	__wine_unixlib_entry_t const __wine_unix_call_wow64_funcs[] = {
		::__wine_unix::wow64_unix_host_fd_to_unix_fd,
		::__wine_unix::wow64_unix_unix_fd_to_host_fd,
		::__wine_unix::wow64_unix_host_fd_to_nt_handle,
		::__wine_unix::wow64_unix_nt_handle_to_host_fd,
		::__wine_unix::wow64_unix_openat,
		::__wine_unix::wow64_unix_close,
		::__wine_unix::wow64_unix_writev,
		::__wine_unix::wow64_unix_readv,
		::__wine_unix::wow64_unix_pwritev,
		::__wine_unix::wow64_unix_preadv,
		::__wine_unix::wow64_unix_write,
		::__wine_unix::wow64_unix_read,
		::__wine_unix::wow64_unix_get_std_host_fd,
		::__wine_unix::wow64_unix_at_fdcwd,
		::__wine_unix::wow64_unix_open,
		::__wine_unix::unix_is_unix, /* params layout is identical for both ABIs */
	};
#endif

	__wine_unix_status_t __wine_unix_lib_init(void) noexcept
	{
		/* resolve the wineserver conversion entry points once, at .so load */
		if (void *const ntdll{::dlopen("ntdll.so", RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD)})
		{
			wine_server_fd_to_handle_p =
				reinterpret_cast<wine_server_fd_to_handle_t *>(::dlsym(ntdll, "wine_server_fd_to_handle"));
			wine_server_handle_to_fd_p =
				reinterpret_cast<wine_server_handle_to_fd_t *>(::dlsym(ntdll, "wine_server_handle_to_fd"));
			rtl_get_current_peb_p =
				reinterpret_cast<rtl_get_current_peb_t *>(::dlsym(ntdll, "RtlGetCurrentPeb"));
			nt_compare_objects_p =
				reinterpret_cast<nt_compare_objects_t *>(::dlsym(ntdll, "NtCompareObjects"));
		}
		return __WINE_UNIX_ERRNO_SUCCESS;
	}
} // extern "C"

static_assert(sizeof(__wine_unix_call_funcs) / sizeof(__wine_unixlib_entry_t) == __wine_unix_call_funcs_count,
			  "__wine_unix_call_funcs must match the __wine_unix_funcs enum");
#if INTPTR_MAX >= INT64_MAX
static_assert(sizeof(__wine_unix_call_wow64_funcs) / sizeof(__wine_unixlib_entry_t) == __wine_unix_call_funcs_count,
			  "__wine_unix_call_wow64_funcs must match the __wine_unix_funcs enum");
#endif
