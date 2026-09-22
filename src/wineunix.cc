/*
wineunix.dll (unixcall implementation) — built for the wine side.

	clang++ --config=$HOME/herbcfgs/x86_64-windows-msvc.cfg -fherbceptions -shared \
		-o wineunix.dll src/wineunix.cc -Iinclude -lntdll

Each exported __wine_unix_* call is packed into its params struct and run
through ntdll's __wine_unix_call_dispatcher into libwineunix.so, which does the
real host libc call. The params ABI is private to this dll<->so boundary
(__wine_unix_abi.h).

The dispatcher and the unixlib's call table are resolved lazily on the first
call: no dispatcher (real windows) means this dll is the wrong implementation —
every call reports ENOSYS and the nt-implementation wineunix.dll should be
shipped instead.
*/

#define __WINE_UNIX_DLL_BUILD

#include <__wine_unix/__wine_unix.h>
#include <__wine_unix/__wine_unix_abi.h>

#include <cstdint>

namespace
{

/* ---- minimal NT declarations (ntdll.lib) --------------------------------- */

struct unicode_string
{
	uint16_t len;
	uint16_t maxlen;
	char16_t *buf;

	constexpr unicode_string(char16_t *s, ::std::uint16_t n) noexcept
		: len{static_cast<::std::uint16_t>(n * sizeof(char16_t))},
		  maxlen{static_cast<::std::uint16_t>(len + sizeof(char16_t))}, buf{s}
	{
	}
};

struct ansi_string
{
	uint16_t len;
	uint16_t maxlen;
	char *buf;
};

extern "C"
{
	__declspec(dllimport) int32_t __stdcall NtQueryVirtualMemory(void *process, void const *addr,
															   uint32_t info_class, void *buffer, size_t len,
															   size_t *res_len) noexcept;
	__declspec(dllimport) int32_t __stdcall LdrGetDllHandle(char16_t const *path, uint32_t *characteristics,
														  unicode_string *name, void **handle) noexcept;
	__declspec(dllimport) int32_t __stdcall LdrGetProcedureAddress(void *handle, ansi_string const *name,
																 uint32_t ordinal, void **proc) noexcept;
}

constexpr uint32_t memory_wine_load_unix_lib_by_name{1002};

__wine_unixlib_entry_t const *funcs;
__wine_unix_call_dispatcher_t dispatcher;

bool resolve() noexcept
{
	char16_t ntdll_name[] = u"ntdll.dll";
	unicode_string us{ntdll_name, 12};
	void *ntdll{};
	if (LdrGetDllHandle(nullptr, nullptr, &us, &ntdll) || ntdll == nullptr)
	{
		return false;
	}
	/*
	__wine_unix_call_dispatcher is a data export: a variable in ntdll.dll
	holding the unix-side dispatcher address. LdrGetProcedureAddress gives the
	variable's address; dereference once for the callable pointer. Absent on
	real windows.
	*/
	char8_t disp_name[] = u8"__wine_unix_call_dispatcher";
	ansi_string disp_as{static_cast<uint16_t>(sizeof(disp_name) - 1),
						static_cast<uint16_t>(sizeof(disp_name)), reinterpret_cast<char*>(disp_name)};
	void *disp_var{};
	if (LdrGetProcedureAddress(ntdll, &disp_as, 0, &disp_var) || disp_var == nullptr)
	{
		return false;
	}
	auto *disp{*static_cast<__wine_unix_call_dispatcher_t *>(disp_var)};

	/* load the host unixlib by name (WINEDLLPATH dirs + wine's own lib dir) */
	char16_t lib_name[] = u"libwineunix";
	unicode_string lib_us{lib_name, 11};
	uint_least64_t res[2]{};
	size_t reslen{};
	if (NtQueryVirtualMemory(reinterpret_cast<void *>(static_cast<uintptr_t>(-1)), &lib_us,
							 memory_wine_load_unix_lib_by_name, res, sizeof(res), &reslen) ||
		res[1] == 0)
	{
		return false;
	}
	funcs = reinterpret_cast<__wine_unixlib_entry_t const *>(static_cast<uintptr_t>(res[1]));
	dispatcher = disp;
	return true;
}

__wine_unix_status_t call(unsigned int code, void *args) noexcept
{
	static bool const ok{resolve()};
	if (!ok)
	{
		return __WINE_UNIX_ERRNO_ENOSYS;
	}
	return dispatcher(reinterpret_cast<__wine_unixlib_handle_t>(funcs), code, args);
}

} // namespace

extern "C"
{
	__WINE_UNIX_API __wine_unix_unix_fd_status_t
	__wine_unix_host_fd_to_unix_fd_returns_status(__wine_host_fd_t host_fd) noexcept
	{
		__wine_unix_host_fd_to_unix_fd_params_t p{host_fd, -1};
		return {call(__wine_unix_call_host_fd_to_unix_fd, &p), p.unix_fd};
	}

	__WINE_UNIX_API __wine_unix_host_fd_status_t
	__wine_unix_unix_fd_to_host_fd_returns_status(int unix_fd) noexcept
	{
		__wine_unix_unix_fd_to_host_fd_params_t p{unix_fd, 0};
		return {call(__wine_unix_call_unix_fd_to_host_fd, &p), p.host_fd};
	}

	__WINE_UNIX_API __wine_unix_nt_handle_status_t
	__wine_unix_host_fd_to_nt_handle_returns_status(__wine_host_fd_t host_fd) noexcept
	{
		__wine_unix_host_fd_to_nt_handle_params_t p{host_fd, 0};
		return {call(__wine_unix_call_host_fd_to_nt_handle, &p), p.handle};
	}

	__WINE_UNIX_API __wine_unix_host_fd_status_t
	__wine_unix_nt_handle_to_host_fd_returns_status(ptrdiff_t handle) noexcept
	{
		__wine_unix_nt_handle_to_host_fd_params_t p{handle, 0};
		return {call(__wine_unix_call_nt_handle_to_host_fd, &p), p.host_fd};
	}

	__WINE_UNIX_API __wine_unix_host_fd_status_t
	__wine_unix_openat_returns_status(__wine_host_fd_t host_dirfd, char const *filename, size_t filenamelen,
									  __wine_host_flags_t flags, __wine_host_mode_t mode) noexcept
	{
		__wine_unix_openat_params_t p{host_dirfd, filename, filenamelen, flags, mode, 0};
		return {call(__wine_unix_call_openat, &p), p.host_fd};
	}

	__WINE_UNIX_API __wine_unix_status_t __wine_unix_close_returns_status(__wine_host_fd_t host_fd) noexcept
	{
		__wine_unix_close_params_t p{host_fd};
		return call(__wine_unix_call_close, &p);
	}

	__WINE_UNIX_API __wine_unix_rwv_status_t
	__wine_unix_writev_returns_status(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									  size_t iovsize) noexcept
	{
		__wine_unix_readwritev_params_t p{host_fd, iovs, iovsize, 0, 0, 0};
		auto const st{call(__wine_unix_call_writev, &p)};
		return {st, p.total, p.baseindex, p.index};
	}

	__WINE_UNIX_API __wine_unix_rwv_status_t
	__wine_unix_readv_returns_status(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									 size_t iovsize) noexcept
	{
		__wine_unix_readwritev_params_t p{host_fd, iovs, iovsize, 0, 0, 0};
		auto const st{call(__wine_unix_call_readv, &p)};
		return {st, p.total, p.baseindex, p.index};
	}

	__WINE_UNIX_API __wine_unix_rwv_status_t
	__wine_unix_pwritev_returns_status(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									   size_t iovsize, __wine_off_t offset) noexcept
	{
		__wine_unix_preadwritev_params_t p{host_fd, iovs, iovsize, offset, 0, 0, 0};
		auto const st{call(__wine_unix_call_pwritev, &p)};
		return {st, p.total, p.baseindex, p.index};
	}

	__WINE_UNIX_API __wine_unix_rwv_status_t
	__wine_unix_preadv_returns_status(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									  size_t iovsize, __wine_off_t offset) noexcept
	{
		__wine_unix_preadwritev_params_t p{host_fd, iovs, iovsize, offset, 0, 0, 0};
		auto const st{call(__wine_unix_call_preadv, &p)};
		return {st, p.total, p.baseindex, p.index};
	}

	__WINE_UNIX_API __wine_unix_rw_status_t
	__wine_unix_write_returns_status(__wine_host_fd_t host_fd, void const *buf, size_t len) noexcept
	{
		__wine_unix_readwrite_params_t p{host_fd, const_cast<void *>(buf), len, 0};
		auto const st{call(__wine_unix_call_write, &p)};
		return {st, p.total};
	}

	__WINE_UNIX_API __wine_unix_rw_status_t
	__wine_unix_read_returns_status(__wine_host_fd_t host_fd, void *buf, size_t len) noexcept
	{
		__wine_unix_readwrite_params_t p{host_fd, buf, len, 0};
		auto const st{call(__wine_unix_call_read, &p)};
		return {st, p.total};
	}

	__WINE_UNIX_API __WINE_UNIX_CONST __wine_unix_host_fd_status_t
	__wine_unix_get_std_host_fd_returns_status(int which) noexcept
	{
		__wine_unix_get_std_host_fd_params_t p{which, 0};
		return {call(__wine_unix_call_get_std_host_fd, &p), p.host_fd};
	}

#if defined(__HERBCEPTIONS__)

	__WINE_UNIX_API int __wine_unix_host_fd_to_unix_fd(__wine_host_fd_t host_fd) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_host_fd_to_unix_fd_returns_status(host_fd)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return r.unix_fd;
	}

	__WINE_UNIX_API __wine_host_fd_t __wine_unix_unix_fd_to_host_fd(int unix_fd) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_unix_fd_to_host_fd_returns_status(unix_fd)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return r.host_fd;
	}

	__WINE_UNIX_API ptrdiff_t __wine_unix_host_fd_to_nt_handle(__wine_host_fd_t host_fd) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_host_fd_to_nt_handle_returns_status(host_fd)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return r.handle;
	}

	__WINE_UNIX_API __wine_host_fd_t __wine_unix_nt_handle_to_host_fd(ptrdiff_t handle) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_nt_handle_to_host_fd_returns_status(handle)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return r.host_fd;
	}

	__WINE_UNIX_API __wine_host_fd_t __wine_unix_openat(__wine_host_fd_t host_dirfd, char const *filename,
														size_t filenamelen, __wine_host_flags_t flags,
														__wine_host_mode_t mode) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_openat_returns_status(host_dirfd, filename, filenamelen, flags, mode)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return r.host_fd;
	}

	__WINE_UNIX_API void __wine_unix_close(__wine_host_fd_t host_fd) return_failure{__wine_unix_errc}
	{
		if (auto const st{__wine_unix_close_returns_status(host_fd)}; st != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(st);
		}
	}

	__WINE_UNIX_API __wine_unix_rwv_result_t __wine_unix_writev(__wine_host_fd_t host_fd,
																__wine_unix_iovec_t const *iovs,
																size_t iovsize) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_writev_returns_status(host_fd, iovs, iovsize)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return {r.total, r.baseindex, r.index};
	}

	__WINE_UNIX_API __wine_unix_rwv_result_t __wine_unix_readv(__wine_host_fd_t host_fd,
															   __wine_unix_iovec_t const *iovs,
															   size_t iovsize) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_readv_returns_status(host_fd, iovs, iovsize)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return {r.total, r.baseindex, r.index};
	}

	__WINE_UNIX_API __wine_unix_rwv_result_t __wine_unix_pwritev(__wine_host_fd_t host_fd,
																 __wine_unix_iovec_t const *iovs,
																 size_t iovsize,
																 __wine_off_t offset) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_pwritev_returns_status(host_fd, iovs, iovsize, offset)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return {r.total, r.baseindex, r.index};
	}

	__WINE_UNIX_API __wine_unix_rwv_result_t __wine_unix_preadv(__wine_host_fd_t host_fd,
																__wine_unix_iovec_t const *iovs,
																size_t iovsize,
																__wine_off_t offset) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_preadv_returns_status(host_fd, iovs, iovsize, offset)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return {r.total, r.baseindex, r.index};
	}

	__WINE_UNIX_API __wine_unix_rw_result_t __wine_unix_write(__wine_host_fd_t host_fd, void const *buf,
															size_t len) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_write_returns_status(host_fd, buf, len)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return {r.total};
	}

	__WINE_UNIX_API __wine_unix_rw_result_t __wine_unix_read(__wine_host_fd_t host_fd, void *buf,
														   size_t len) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_read_returns_status(host_fd, buf, len)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return {r.total};
	}

	__WINE_UNIX_API __WINE_UNIX_CONST __wine_host_fd_t
	__wine_unix_get_std_host_fd(int which) return_failure{__wine_unix_errc}
	{
		auto const r{__wine_unix_get_std_host_fd_returns_status(which)};
		if (r.status != __WINE_UNIX_ERRNO_SUCCESS)
		{
			return_failure static_cast<__wine_unix_errc>(r.status);
		}
		return r.host_fd;
	}

#endif

	__declspec(dllexport) int __stdcall DllMain(void *, uint32_t, void *) noexcept
	{
		return 1;
	}
}
