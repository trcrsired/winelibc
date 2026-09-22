#pragma once
/*
public api of wineunix.dll. the same exports ship in two implementations:

	wineunix.dll (unixcall): user -> wineunix.dll -> libwineunix.so -> host libc
	wineunix.dll (nt):       user -> wineunix.dll -> ntdll

swap the dll and the same binary runs on wine or real windows.

two forms per call:

	__wine_unix_<op>_returns_status(...)
		returns a struct whose first member is the status (0 = success,
		otherwise an errno value from __wine_unix_errno.h).

	__wine_unix_<op>(...) return_failure{__wine_unix_errc}
		herbceptions builds only; failure rides the fails channel as
		wine_errc and auto-propagates as std::error inside throws
		functions via error_domain<wine_errc>.

the params-struct marshalling between wineunix.dll and libwineunix.so is
private to the two modules; see __wine_unix_abi.h.
*/

#include <stdint.h>
#include <limits.h>
#include <stddef.h>

#include "__wine_unix_errno.h"
#include "__wine_unix_fcntl.h"

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(__WINE_UNIX_DLL_BUILD)
#define __WINE_UNIX_API __declspec(dllexport)
#else
#define __WINE_UNIX_API __declspec(dllimport)
#endif
#else
#define __WINE_UNIX_API
#endif

#if defined(__GNUC__) || defined(__clang__)
#define __WINE_UNIX_CONST __attribute__((__const__))
#else
#define __WINE_UNIX_CONST
#endif

#ifdef __cplusplus
#if __cplusplus <= 201107
#define __WINE_UNIX_NOEXCEPT throw()
#else
#define __WINE_UNIX_NOEXCEPT noexcept
#endif
extern "C"
{
#else
#define __WINE_UNIX_NOEXCEPT
#endif

#if INTPTR_MAX < INT_LEAST32_MAX
	typedef uint_least32_t __wine_host_fd_t;
	typedef uint_least32_t __wine_errno_t;
#else
typedef size_t __wine_host_fd_t;
typedef size_t __wine_errno_t;
#endif
#if INTPTR_MAX < INT_LEAST64_MAX
	typedef int_least64_t __wine_off_t;
#else
typedef ptrdiff_t __wine_off_t;
#endif

	typedef __wine_host_fd_t __wine_host_flags_t;
	typedef __wine_host_fd_t __wine_host_mode_t;

	/* matches struct iovec layout on every unix host. */
	typedef struct
	{
		void const *iov_base;
		size_t iov_len;
	} __wine_unix_iovec_t;

	/* uniform status type: 0 = success, otherwise an errno value. */
	typedef int_least32_t __wine_unix_status_t;

	/*
	host_fd encoding is the wineunix.dll implementation's own; 0 always
	means "no fd":
	  unixcall impl: unix fd + 1 (through libwineunix.so)
	  nt impl:       raw windows HANDLE (never 0 when valid)
	*/

	typedef struct
	{
		__wine_unix_status_t status;
		int unix_fd;
	} __wine_unix_unix_fd_status_t;

	typedef struct
	{
		__wine_unix_status_t status;
		__wine_host_fd_t host_fd;
	} __wine_unix_host_fd_status_t;

	typedef struct
	{
		__wine_unix_status_t status;
		ptrdiff_t handle;
	} __wine_unix_nt_handle_status_t;

	typedef struct
	{
		__wine_unix_status_t status;
		size_t total;
		size_t baseindex;
		size_t index;
	} __wine_unix_rwv_status_t;

	/* success-side value of the vectored calls. */
	typedef struct
	{
		size_t total;
		size_t baseindex;
		size_t index;
	} __wine_unix_rwv_result_t;

	typedef struct
	{
		__wine_unix_status_t status;
		size_t total;
	} __wine_unix_rw_status_t;

	/* success-side value of the plain (non-vectored) read/write calls. */
	typedef struct
	{
		size_t total;
	} __wine_unix_rw_result_t;

	__WINE_UNIX_API __wine_unix_unix_fd_status_t __wine_unix_host_fd_to_unix_fd_returns_status(__wine_host_fd_t host_fd) __WINE_UNIX_NOEXCEPT;
	__WINE_UNIX_API __wine_unix_host_fd_status_t __wine_unix_unix_fd_to_host_fd_returns_status(int unix_fd) __WINE_UNIX_NOEXCEPT;
	/*
	host_fd <-> HANDLE conversions TRANSFER ownership: on success the source is
	consumed (caller must not use or close it) and the result is owned by the
	caller. On failure the source's state is unspecified — do not close it.
	*/
	__WINE_UNIX_API __wine_unix_nt_handle_status_t __wine_unix_host_fd_to_nt_handle_returns_status(__wine_host_fd_t host_fd) __WINE_UNIX_NOEXCEPT;
	__WINE_UNIX_API __wine_unix_host_fd_status_t __wine_unix_nt_handle_to_host_fd_returns_status(ptrdiff_t handle) __WINE_UNIX_NOEXCEPT;

	__WINE_UNIX_API __wine_unix_host_fd_status_t __wine_unix_openat_returns_status(__wine_host_fd_t host_dirfd,
																				   char const *filename,
																				   size_t filenamelen,
																				   __wine_host_flags_t flags,
																				   __wine_host_mode_t mode) __WINE_UNIX_NOEXCEPT;
	__WINE_UNIX_API __wine_unix_status_t __wine_unix_close_returns_status(__wine_host_fd_t host_fd) __WINE_UNIX_NOEXCEPT;
	__WINE_UNIX_API __wine_unix_rwv_status_t __wine_unix_writev_returns_status(__wine_host_fd_t host_fd,
																			   __wine_unix_iovec_t const *iovs,
																			   size_t iovsize) __WINE_UNIX_NOEXCEPT;
	__WINE_UNIX_API __wine_unix_rwv_status_t __wine_unix_readv_returns_status(__wine_host_fd_t host_fd,
																			  __wine_unix_iovec_t const *iovs,
																			  size_t iovsize) __WINE_UNIX_NOEXCEPT;
	__WINE_UNIX_API __wine_unix_rwv_status_t __wine_unix_pwritev_returns_status(__wine_host_fd_t host_fd,
																				__wine_unix_iovec_t const *iovs,
																				size_t iovsize,
																				__wine_off_t offset) __WINE_UNIX_NOEXCEPT;
	__WINE_UNIX_API __wine_unix_rwv_status_t __wine_unix_preadv_returns_status(__wine_host_fd_t host_fd,
																			   __wine_unix_iovec_t const *iovs,
																			   size_t iovsize,
																			   __wine_off_t offset) __WINE_UNIX_NOEXCEPT;

	/* plain read/write: read_some/write_some semantics, one host op. */
	__WINE_UNIX_API __wine_unix_rw_status_t __wine_unix_write_returns_status(__wine_host_fd_t host_fd,
																			 void const *buf,
																			 size_t len) __WINE_UNIX_NOEXCEPT;
	__WINE_UNIX_API __wine_unix_rw_status_t __wine_unix_read_returns_status(__wine_host_fd_t host_fd,
																			void *buf,
																			size_t len) __WINE_UNIX_NOEXCEPT;

	/*
	std streams: which is 0 stdin, 1 stdout, 2 stderr. unixcall impl returns
	host_fd = which + 1; nt impl returns the process's Standard{Input,Output,
	Error} handle from PEB->ProcessParameters.
	*/
	__WINE_UNIX_API __WINE_UNIX_CONST __wine_unix_host_fd_status_t
	__wine_unix_get_std_host_fd_returns_status(int which) __WINE_UNIX_NOEXCEPT;

#if defined(__cplusplus)
}

/*
the errc enum exists for every c++ consumer: std::wine_errc when herbceptions
is in play, otherwise an empty enum class so code can still name the type and
cast status values to it.
*/
#if defined(__HERBCEPTIONS__) && __has_include(<herbceptions/error>)
#include <herbceptions/error>
using __wine_unix_errc = ::std::wine_errc;
#else
enum class __wine_unix_errc : uint_least32_t
{
};
#endif

extern "C"
{
#if defined(__HERBCEPTIONS__)
	__WINE_UNIX_API int __wine_unix_host_fd_to_unix_fd(__wine_host_fd_t host_fd) return_failure { __wine_unix_errc };
	__WINE_UNIX_API __wine_host_fd_t __wine_unix_unix_fd_to_host_fd(int unix_fd) return_failure { __wine_unix_errc };
	__WINE_UNIX_API ptrdiff_t __wine_unix_host_fd_to_nt_handle(__wine_host_fd_t host_fd) return_failure { __wine_unix_errc };
	__WINE_UNIX_API __wine_host_fd_t __wine_unix_nt_handle_to_host_fd(ptrdiff_t handle) return_failure { __wine_unix_errc };

	__WINE_UNIX_API __wine_host_fd_t __wine_unix_openat(__wine_host_fd_t host_dirfd, char const *filename,
														size_t filenamelen, __wine_host_flags_t flags,
														__wine_host_mode_t mode) return_failure { __wine_unix_errc };
	__WINE_UNIX_API void __wine_unix_close(__wine_host_fd_t host_fd) return_failure { __wine_unix_errc };
	__WINE_UNIX_API __wine_unix_rwv_result_t __wine_unix_writev(__wine_host_fd_t host_fd,
																__wine_unix_iovec_t const *iovs,
																size_t iovsize) return_failure { __wine_unix_errc };
	__WINE_UNIX_API __wine_unix_rwv_result_t __wine_unix_readv(__wine_host_fd_t host_fd,
															   __wine_unix_iovec_t const *iovs,
															   size_t iovsize) return_failure { __wine_unix_errc };
	__WINE_UNIX_API __wine_unix_rwv_result_t __wine_unix_pwritev(__wine_host_fd_t host_fd,
																 __wine_unix_iovec_t const *iovs,
																 size_t iovsize,
																 __wine_off_t offset) return_failure { __wine_unix_errc };
	__WINE_UNIX_API __wine_unix_rwv_result_t __wine_unix_preadv(__wine_host_fd_t host_fd,
																__wine_unix_iovec_t const *iovs,
																size_t iovsize,
																__wine_off_t offset) return_failure { __wine_unix_errc };
	__WINE_UNIX_API __wine_unix_rw_result_t __wine_unix_write(__wine_host_fd_t host_fd, void const *buf,
															  size_t len) return_failure { __wine_unix_errc };
	__WINE_UNIX_API __wine_unix_rw_result_t __wine_unix_read(__wine_host_fd_t host_fd, void *buf,
															 size_t len) return_failure { __wine_unix_errc };
	__WINE_UNIX_API __WINE_UNIX_CONST __wine_host_fd_t
	__wine_unix_get_std_host_fd(int which) return_failure { __wine_unix_errc };
#endif
#endif

#ifdef __cplusplus
}
#endif
