#pragma once
/*
PE-side api for the unixcall contract in __wine_unix.h. C++ only.

Consumers never see params structs. __wine_unix_pe_init() resolves the call
table once: wine's unixlib via NtQueryVirtualMemory + ntdll's
__wine_unix_call_dispatcher, or the winelibc_nt.dll companion (same table,
ntdll-backed) when no dispatcher exists (real windows, or forced).

Every call has two forms:

	__wine_unix_*_status_t __wine_unix_<op>_returns_status(...) noexcept
		returns a struct whose first member is the status (0 = success).

	<value> __wine_unix_<op>(...) return_failure{__wine_unix_errc}
		herbceptions only; failure rides the fails channel as wine_errc.
		Inside a throws function it auto-propagates as std::error because
		libherbceptions specializes error_domain<wine_errc>.
*/

#include <cstddef>
#include <cstdint>

#include "__wine_unix.h"
#include "__wine_unix_errno.h"
#include "__wine_unix_fcntl.h"

#if defined(__HERBCEPTIONS__) && __has_include(<herbceptions/error>)
#include <herbceptions/error>
using __wine_unix_errc = ::std::wine_errc;
#define __WINE_UNIX_HAS_STD_ERRC 1
#else
enum class __wine_unix_errc : ::std::uint_least32_t
{
};
#endif

/*
ntdll declarations needed to resolve the call table. Kept self-contained so
the header does not depend on any sdk headers.
*/
struct __wine_unix_nt_unicode_string
{
	::std::uint16_t len;
	::std::uint16_t maxlen;
	char16_t *buf;

	constexpr __wine_unix_nt_unicode_string(char16_t *s, ::std::uint16_t n) noexcept
		: len{static_cast<::std::uint16_t>(n * sizeof(char16_t))},
		  maxlen{static_cast<::std::uint16_t>(len + sizeof(char16_t))}, buf{s}
	{
	}
};

struct __wine_unix_nt_ansi_string
{
	::std::uint16_t len;
	::std::uint16_t maxlen;
	char *buf;
};

extern "C"
{
	__declspec(dllimport) ::std::int32_t __stdcall NtQueryVirtualMemory(void *process, void const *addr,
																	  ::std::uint32_t info_class, void *buffer,
																	  ::std::size_t len, ::std::size_t *res_len) noexcept;
	__declspec(dllimport) ::std::int32_t __stdcall LdrGetDllHandle(char16_t const *path, ::std::uint32_t *characteristics,
																 __wine_unix_nt_unicode_string *name, void **handle) noexcept;
	__declspec(dllimport) ::std::int32_t __stdcall LdrGetProcedureAddress(void *handle, __wine_unix_nt_ansi_string const *name,
																		::std::uint32_t ordinal, void **proc) noexcept;
	__declspec(dllimport) ::std::int32_t __stdcall LdrLoadDll(char16_t const *path, ::std::uint32_t *characteristics,
															__wine_unix_nt_unicode_string *name, void **handle) noexcept;
}

inline constexpr ::std::uint32_t __wine_unix_memory_load_unix_lib_by_name{1002};

/* resolved call state. __wine_unix_pe_init fills it; embedders may set it directly. */
inline __wine_unixlib_entry_t const *__wine_unix_pe_funcs{};
inline __wine_unix_call_dispatcher_t __wine_unix_pe_dispatcher{};

inline __wine_unix_status_t __wine_unix_pe_call(unsigned int code, void *args) noexcept
{
	if (__wine_unix_pe_dispatcher != nullptr)
	{
		return __wine_unix_pe_dispatcher(reinterpret_cast<__wine_unixlib_handle_t>(__wine_unix_pe_funcs), code, args);
	}
	return __wine_unix_pe_funcs[code](args);
}

/* resolve ntdll's unixcall dispatcher. missing under real windows: not an error. */
inline ::std::int32_t __wine_unix_pe_resolve_dispatcher() noexcept
{
	char16_t ntdll_name[] = u"ntdll.dll";
	__wine_unix_nt_unicode_string us{ntdll_name, 12};
	void *ntdll{};
	if (auto st{LdrGetDllHandle(nullptr, nullptr, &us, &ntdll)}; st || ntdll == nullptr)
	{
		return st ? st : -1;
	}
	char name[] = "__wine_unix_call_dispatcher";
	__wine_unix_nt_ansi_string as{static_cast<::std::uint16_t>(sizeof(name) - 1),
								  static_cast<::std::uint16_t>(sizeof(name)), name};
	void *var{};
	if (auto st{LdrGetProcedureAddress(ntdll, &as, 0, &var)}; st || var == nullptr)
	{
		return st ? st : -1;
	}
	/* the export is a variable holding the dispatcher address: dereference once. */
	__wine_unix_pe_dispatcher = *static_cast<__wine_unix_call_dispatcher_t *>(var);
	return 0;
}

/* load a host unixlib by name (WINEDLLPATH dirs + wine's own lib dir). */
inline ::std::int32_t __wine_unix_pe_load_unixlib(char16_t const *name, ::std::uint16_t namelen) noexcept
{
	char16_t buf[256];
	if (namelen + 1 > 256)
	{
		return -1;
	}
	for (::std::uint16_t i{}; i < namelen; ++i)
	{
		buf[i] = name[i];
	}
	buf[namelen] = 0;
	__wine_unix_nt_unicode_string us{buf, namelen};
	::std::uint_least64_t res[2]{};
	::std::size_t reslen{};
	auto st{NtQueryVirtualMemory(reinterpret_cast<void *>(static_cast<::std::uintptr_t>(-1)), &us,
								 __wine_unix_memory_load_unix_lib_by_name, res, sizeof(res), &reslen)};
	if (st || res[1] == 0)
	{
		return st ? st : -1;
	}
	__wine_unix_pe_funcs = reinterpret_cast<__wine_unixlib_entry_t const *>(static_cast<::std::uintptr_t>(res[1]));
	return 0;
}

/* load winelibc_nt.dll and point the table at its exported __wine_unix_call_funcs. */
inline ::std::int32_t __wine_unix_pe_init_nt() noexcept
{
	char16_t dll_name[] = u"winelibc_nt";
	__wine_unix_nt_unicode_string us{dll_name, 11};
	void *dll{};
	if (auto st{LdrLoadDll(nullptr, nullptr, &us, &dll)}; st || dll == nullptr)
	{
		return st ? st : -1;
	}
	char name[] = "__wine_unix_call_funcs";
	__wine_unix_nt_ansi_string as{static_cast<::std::uint16_t>(sizeof(name) - 1),
								  static_cast<::std::uint16_t>(sizeof(name)), name};
	void *var{};
	if (auto st{LdrGetProcedureAddress(dll, &as, 0, &var)}; st || var == nullptr)
	{
		return st ? st : -1;
	}
	__wine_unix_pe_funcs = static_cast<__wine_unixlib_entry_t const *>(var);
	__wine_unix_pe_dispatcher = nullptr;
	return 0;
}

/* one-shot: try the wine unixlib, fall back to winelibc_nt.dll. */
inline ::std::int32_t __wine_unix_pe_init(char16_t const *unixlib_name, ::std::uint16_t unixlib_namelen) noexcept
{
	if (__wine_unix_pe_resolve_dispatcher() == 0 && __wine_unix_pe_load_unixlib(unixlib_name, unixlib_namelen) == 0)
	{
		return 0;
	}
	return __wine_unix_pe_init_nt();
}

/* result structs: status first, then the call's outputs. */

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
	::std::ptrdiff_t handle;
} __wine_unix_nt_handle_status_t;

typedef struct
{
	__wine_unix_status_t status;
	::std::size_t total;
	::std::size_t baseindex;
	::std::size_t index;
} __wine_unix_rwv_status_t;

/* success-side value of the vectored calls. */
typedef struct
{
	::std::size_t total;
	::std::size_t baseindex;
	::std::size_t index;
} __wine_unix_rwv_result_t;

inline __wine_unix_unix_fd_status_t __wine_unix_host_fd_to_unix_fd_returns_status(__wine_host_fd_t host_fd) noexcept
{
	__wine_unix_host_fd_to_unix_fd_params_t p{host_fd, -1};
	return {__wine_unix_pe_call(__wine_unix_call_host_fd_to_unix_fd, &p), p.unix_fd};
}

inline __wine_unix_host_fd_status_t __wine_unix_unix_fd_to_host_fd_returns_status(int unix_fd) noexcept
{
	__wine_unix_unix_fd_to_host_fd_params_t p{unix_fd, 0};
	return {__wine_unix_pe_call(__wine_unix_call_unix_fd_to_host_fd, &p), p.host_fd};
}

inline __wine_unix_nt_handle_status_t __wine_unix_host_fd_to_nt_handle_returns_status(__wine_host_fd_t host_fd) noexcept
{
	__wine_unix_host_fd_to_nt_handle_params_t p{host_fd, 0};
	return {__wine_unix_pe_call(__wine_unix_call_host_fd_to_nt_handle, &p), p.handle};
}

inline __wine_unix_host_fd_status_t __wine_unix_nt_handle_to_host_fd_returns_status(::std::ptrdiff_t handle) noexcept
{
	__wine_unix_nt_handle_to_host_fd_params_t p{handle, 0};
	return {__wine_unix_pe_call(__wine_unix_call_nt_handle_to_host_fd, &p), p.host_fd};
}

inline __wine_unix_host_fd_status_t __wine_unix_openat_returns_status(__wine_host_fd_t host_dirfd,
																	  char const *filename, ::std::size_t filenamelen,
																	  __wine_host_flags_t flags,
																	  __wine_host_mode_t mode) noexcept
{
	__wine_unix_openat_params_t p{host_dirfd, filename, filenamelen, flags, mode, 0};
	return {__wine_unix_pe_call(__wine_unix_call_openat, &p), p.host_fd};
}

inline __wine_unix_status_t __wine_unix_close_returns_status(__wine_host_fd_t host_fd) noexcept
{
	__wine_unix_close_params_t p{host_fd};
	return __wine_unix_pe_call(__wine_unix_call_close, &p);
}

inline __wine_unix_rwv_status_t __wine_unix_writev_returns_status(__wine_host_fd_t host_fd,
																  __wine_unix_iovec_t const *iovs,
																  ::std::size_t iovsize) noexcept
{
	__wine_unix_readwritev_params_t p{host_fd, iovs, iovsize, 0, 0, 0};
	auto st{__wine_unix_pe_call(__wine_unix_call_writev, &p)};
	return {st, p.total, p.baseindex, p.index};
}

inline __wine_unix_rwv_status_t __wine_unix_readv_returns_status(__wine_host_fd_t host_fd,
																 __wine_unix_iovec_t const *iovs,
																 ::std::size_t iovsize) noexcept
{
	__wine_unix_readwritev_params_t p{host_fd, iovs, iovsize, 0, 0, 0};
	auto st{__wine_unix_pe_call(__wine_unix_call_readv, &p)};
	return {st, p.total, p.baseindex, p.index};
}

inline __wine_unix_rwv_status_t __wine_unix_pwritev_returns_status(__wine_host_fd_t host_fd,
																   __wine_unix_iovec_t const *iovs,
																   ::std::size_t iovsize, __wine_off_t offset) noexcept
{
	__wine_unix_preadwritev_params_t p{host_fd, iovs, iovsize, offset, 0, 0, 0};
	auto st{__wine_unix_pe_call(__wine_unix_call_pwritev, &p)};
	return {st, p.total, p.baseindex, p.index};
}

inline __wine_unix_rwv_status_t __wine_unix_preadv_returns_status(__wine_host_fd_t host_fd,
																  __wine_unix_iovec_t const *iovs,
																  ::std::size_t iovsize, __wine_off_t offset) noexcept
{
	__wine_unix_preadwritev_params_t p{host_fd, iovs, iovsize, offset, 0, 0, 0};
	auto st{__wine_unix_pe_call(__wine_unix_call_preadv, &p)};
	return {st, p.total, p.baseindex, p.index};
}

#if defined(__HERBCEPTIONS__)

inline int __wine_unix_host_fd_to_unix_fd(__wine_host_fd_t host_fd) return_failure{__wine_unix_errc}
{
	auto r{__wine_unix_host_fd_to_unix_fd_returns_status(host_fd)};
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(r.status);
	}
	return r.unix_fd;
}

inline __wine_host_fd_t __wine_unix_unix_fd_to_host_fd(int unix_fd) return_failure{__wine_unix_errc}
{
	auto r{__wine_unix_unix_fd_to_host_fd_returns_status(unix_fd)};
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(r.status);
	}
	return r.host_fd;
}

inline ::std::ptrdiff_t __wine_unix_host_fd_to_nt_handle(__wine_host_fd_t host_fd) return_failure{__wine_unix_errc}
{
	auto r{__wine_unix_host_fd_to_nt_handle_returns_status(host_fd)};
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(r.status);
	}
	return r.handle;
}

inline __wine_host_fd_t __wine_unix_nt_handle_to_host_fd(::std::ptrdiff_t handle) return_failure{__wine_unix_errc}
{
	auto r{__wine_unix_nt_handle_to_host_fd_returns_status(handle)};
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(r.status);
	}
	return r.host_fd;
}

inline __wine_host_fd_t __wine_unix_openat(__wine_host_fd_t host_dirfd, char const *filename,
										   ::std::size_t filenamelen, __wine_host_flags_t flags,
										   __wine_host_mode_t mode) return_failure{__wine_unix_errc}
{
	auto r{__wine_unix_openat_returns_status(host_dirfd, filename, filenamelen, flags, mode)};
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(r.status);
	}
	return r.host_fd;
}

inline void __wine_unix_close(__wine_host_fd_t host_fd) return_failure{__wine_unix_errc}
{
	if (auto st{__wine_unix_close_returns_status(host_fd)}; st != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(st);
	}
}

inline __wine_unix_rwv_result_t __wine_unix_writev(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
												   ::std::size_t iovsize) return_failure{__wine_unix_errc}
{
	auto r{__wine_unix_writev_returns_status(host_fd, iovs, iovsize)};
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(r.status);
	}
	return {r.total, r.baseindex, r.index};
}

inline __wine_unix_rwv_result_t __wine_unix_readv(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
												  ::std::size_t iovsize) return_failure{__wine_unix_errc}
{
	auto r{__wine_unix_readv_returns_status(host_fd, iovs, iovsize)};
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(r.status);
	}
	return {r.total, r.baseindex, r.index};
}

inline __wine_unix_rwv_result_t __wine_unix_pwritev(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
													::std::size_t iovsize, __wine_off_t offset) return_failure{__wine_unix_errc}
{
	auto r{__wine_unix_pwritev_returns_status(host_fd, iovs, iovsize, offset)};
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(r.status);
	}
	return {r.total, r.baseindex, r.index};
}

inline __wine_unix_rwv_result_t __wine_unix_preadv(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
												   ::std::size_t iovsize, __wine_off_t offset) return_failure{__wine_unix_errc}
{
	auto r{__wine_unix_preadv_returns_status(host_fd, iovs, iovsize, offset)};
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return_failure static_cast<__wine_unix_errc>(r.status);
	}
	return {r.total, r.baseindex, r.index};
}

#endif
