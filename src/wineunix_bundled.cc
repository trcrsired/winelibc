/*
wineunix.dll (bundled implementation) — one dll that works on both sides.

	clang++ --config=$HOME/herbcfgs/x86_64-windows-msvc.cfg -fherbceptions -shared \
		-o wineunix.dll src/wineunix_bundled.cc -Iinclude -lntdll

The public exports come from wineunix.cc unchanged: every __wine_unix_* call is
packed into its params struct and run through call(). call() dispatches through
a funcs table + dispatcher pair — normally the pair resolved from
libwineunix.so via ntdll's __wine_unix_call_dispatcher.

When that resolution fails (real windows, or wine without libwineunix.so),
call() points the pair at nt_bundle_call_funcs + nt_bundle_dispatch instead:
a local table of handlers, one per call code, that unpack the params struct
and run the nt implementation (wineunix_nt.cc) in-process.

	user -> wineunix.dll -> libwineunix.so -> libc   (under wine)
	user -> wineunix.dll -> ntdll                    (everywhere else)

wineunix.cc is #included so its machinery (resolve/call/funcs/dispatcher) and
its exports live in this TU; wineunix_nt.cc is #included with
__WINE_UNIX_NT_INTERNAL__ so only the winelibc_nt implementation compiles,
not its duplicate exports.
*/

#define __WINE_UNIX_BUNDLED__ 1
#define __WINE_UNIX_NT_INTERNAL__ 1

#include "wineunix.cc"
#include "wineunix_nt.cc"

namespace
{

/* ---- nt-side unixcall handlers: params struct in/out, nt impl underneath -- */

__wine_unix_status_t nt_op_host_fd_to_unix_fd(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_host_fd_to_unix_fd_params_t *>(args)};
	auto const r{::winelibc_nt::nt_host_fd_to_unix_fd(p->host_fd)};
	p->unix_fd = r.unix_fd;
	return r.status;
}

__wine_unix_status_t nt_op_unix_fd_to_host_fd(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_unix_fd_to_host_fd_params_t *>(args)};
	auto const r{::winelibc_nt::nt_unix_fd_to_host_fd(p->unix_fd)};
	p->host_fd = r.host_fd;
	return r.status;
}

__wine_unix_status_t nt_op_host_fd_to_nt_handle(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_host_fd_to_nt_handle_params_t *>(args)};
	auto const r{::winelibc_nt::nt_host_fd_to_nt_handle(p->host_fd)};
	p->handle = r.handle;
	return r.status;
}

__wine_unix_status_t nt_op_nt_handle_to_host_fd(void *args) noexcept
{
	/* nt host_fd IS the handle — plain re-encode; nothing closes the source */
	auto *p{static_cast<__wine_unix_nt_handle_to_host_fd_params_t *>(args)};
	auto const r{::winelibc_nt::nt_nt_handle_to_host_fd(p->handle)};
	p->host_fd = r.host_fd;
	return r.status;
}

__wine_unix_status_t nt_op_openat(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_openat_params_t *>(args)};
	auto const r{::winelibc_nt::nt_openat(p->host_dirfd, p->filename, p->filenamelen, p->flags, p->mode)};
	p->host_fd = r.host_fd;
	return r.status;
}

__wine_unix_status_t nt_op_close(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_close_params_t *>(args)};
	return ::winelibc_nt::nt_close(p->host_fd);
}

__wine_unix_status_t nt_op_writev(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_readwritev_params_t *>(args)};
	auto const r{::winelibc_nt::nt_writev(p->host_fd, p->iovs, p->iovsize)};
	p->baseindex = r.baseindex;
	p->index = r.index;
	return r.status;
}

__wine_unix_status_t nt_op_readv(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_readwritev_params_t *>(args)};
	auto const r{::winelibc_nt::nt_readv(p->host_fd, p->iovs, p->iovsize)};
	p->baseindex = r.baseindex;
	p->index = r.index;
	return r.status;
}

__wine_unix_status_t nt_op_pwritev(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_preadwritev_params_t *>(args)};
	auto const r{::winelibc_nt::nt_pwritev(p->host_fd, p->iovs, p->iovsize, p->offset)};
	p->baseindex = r.baseindex;
	p->index = r.index;
	return r.status;
}

__wine_unix_status_t nt_op_preadv(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_preadwritev_params_t *>(args)};
	auto const r{::winelibc_nt::nt_preadv(p->host_fd, p->iovs, p->iovsize, p->offset)};
	p->baseindex = r.baseindex;
	p->index = r.index;
	return r.status;
}

__wine_unix_status_t nt_op_write(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_readwrite_params_t *>(args)};
	auto const r{::winelibc_nt::nt_write(p->host_fd, p->buf, p->len)};
	p->total = r.total;
	return r.status;
}

__wine_unix_status_t nt_op_read(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_readwrite_params_t *>(args)};
	auto const r{::winelibc_nt::nt_read(p->host_fd, p->buf, p->len)};
	p->total = r.total;
	return r.status;
}

__wine_unix_status_t nt_op_get_std_host_fd(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_get_std_host_fd_params_t *>(args)};
	auto const r{::winelibc_nt::nt_get_std_host_fd(p->which)};
	p->host_fd = r.host_fd;
	return r.status;
}

__wine_unix_status_t nt_op_at_fdcwd(void *args) noexcept
{
	auto *p{static_cast<__wine_unix_at_fdcwd_params_t *>(args)};
	p->host_fd = ::winelibc_nt::nt_at_fdcwd_value;
	return __WINE_UNIX_ERRNO_SUCCESS;
}

__wine_unixlib_entry_t const nt_bundle_call_funcs[__wine_unix_call_funcs_count]{
	nt_op_host_fd_to_unix_fd,
	nt_op_unix_fd_to_host_fd,
	nt_op_host_fd_to_nt_handle,
	nt_op_nt_handle_to_host_fd,
	nt_op_openat,
	nt_op_close,
	nt_op_writev,
	nt_op_readv,
	nt_op_pwritev,
	nt_op_preadv,
	nt_op_write,
	nt_op_read,
	nt_op_get_std_host_fd,
	nt_op_at_fdcwd,
};

__wine_unix_status_t __WINE_UNIX_DEFAULTCALL nt_bundle_dispatch(__wine_unixlib_handle_t fns,
																unsigned int code, void *args) noexcept
{
	if (code >= __wine_unix_call_funcs_count)
	{
		return __WINE_UNIX_ERRNO_ENOSYS;
	}
	return reinterpret_cast<__wine_unixlib_entry_t const *>(fns)[code](args);
}

} // namespace
