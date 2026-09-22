/*
PE-side proof of concept for the __wine_unix api (__wine_unix_pe.h).

Run:
	WINEDLLPATH=<build dir> wine winetest.exe       unixcall path (fast_io_wine.so)
	WINEDLLPATH=<build dir> wine winetest.exe nt    winelibc_nt.dll path

Silent: exit status is the only signal.
*/

#include <cstdint>
#include <cstring>

#include <__wine_unix/__wine_unix_pe.h>

int main(int argc, char **argv)
{
	bool const force_nt{argc > 1 && std::strcmp(argv[1], "nt") == 0};
	if (force_nt ? __wine_unix_pe_init_nt() != 0
				 : __wine_unix_pe_init(u"fast_io_wine", 12) != 0)
	{
		return 1;
	}

	/* openat(AT_FDCWD, "/tmp/fast_io_winetest.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) */
	char const path[] = "/tmp/fast_io_winetest.txt";
	auto op{__wine_unix_openat_returns_status(0, path, sizeof(path) - 1,
											  __WINE_UNIX_O_WRONLY | __WINE_UNIX_O_CREAT | __WINE_UNIX_O_TRUNC, 0644)};
	if (op.status != __WINE_UNIX_ERRNO_SUCCESS || op.host_fd == 0)
	{
		return 1;
	}

	/* writev 3 iovecs */
	char const a[] = "hello ";
	char const b[] = "unix ";
	char const c[] = "side\n";
	__wine_unix_iovec_t iov[3]{{a, 6}, {b, 5}, {c, 5}};
#if defined(__HERBCEPTIONS__)
	auto w{catch return_failure(__wine_unix_writev(op.host_fd, iov, 3))};
	if (w.failed || w.value.total != 16)
	{
		return 1;
	}
	/* failure must ride the fails channel as wine_errc */
	auto bad{catch return_failure(__wine_unix_writev(0, iov, 3))};
	if (!bad.failed || bad.error != static_cast<__wine_unix_errc>(__WINE_UNIX_ERRNO_EBADF))
	{
		return 1;
	}
#else
	auto w{__wine_unix_writev_returns_status(op.host_fd, iov, 3)};
	if (w.status != __WINE_UNIX_ERRNO_SUCCESS || w.total != 16)
	{
		return 1;
	}
	if (__wine_unix_writev_returns_status(0, iov, 3).status != __WINE_UNIX_ERRNO_EBADF)
	{
		return 1;
	}
#endif

	/* close, reopen, readv-verify the bytes round-trip */
	if (__wine_unix_close_returns_status(op.host_fd) != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return 1;
	}
	op = __wine_unix_openat_returns_status(0, path, sizeof(path) - 1, __WINE_UNIX_O_RDONLY, 0);
	if (op.status != __WINE_UNIX_ERRNO_SUCCESS || op.host_fd == 0)
	{
		return 1;
	}
	char buf[32]{};
	__wine_unix_iovec_t riov{buf, sizeof(buf) - 1};
	auto r{__wine_unix_readv_returns_status(op.host_fd, &riov, 1)};
	bool const ok{r.status == __WINE_UNIX_ERRNO_SUCCESS &&
				  __wine_unix_close_returns_status(op.host_fd) == __WINE_UNIX_ERRNO_SUCCESS &&
				  r.total == 16 && std::memcmp(buf, "hello unix side\n", 16) == 0};
	return ok ? 0 : 1;
}
