/*
PE-side test for the wineunix.dll api (__wine_unix.h).

The exe imports the flat api from wineunix.dll — which implementation it gets
depends only on which wineunix.dll sits next to it:

	unixcall/winetest.exe -> unixcall wineunix.dll -> libwineunix.so -> libc
	nt/winetest.exe       -> nt wineunix.dll -> ntdll

libwineunix.so itself is found through WINEDLLPATH by the unixcall dll.

Silent: exit status is the only signal.
*/

#include <cstring>

#include <__wine_unix/__wine_unix.h>

int main()
{
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
	if (r.status != __WINE_UNIX_ERRNO_SUCCESS || r.total != 16 ||
		std::memcmp(buf, "hello unix side\n", 16) != 0)
	{
		return 1;
	}

	/* multi-iovec preadv scatters one read across two buffers */
	char x[6]{}, y[11]{};
	__wine_unix_iovec_t riovs[2]{{x, 6}, {y, 10}};
	auto pr{__wine_unix_preadv_returns_status(op.host_fd, riovs, 2, 0)};
	if (pr.status != __WINE_UNIX_ERRNO_SUCCESS || pr.total != 16 ||
		std::memcmp(x, "hello ", 6) != 0 || std::memcmp(y, "unix side\n", 10) != 0)
	{
		return 1;
	}
	if (__wine_unix_close_returns_status(op.host_fd) != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return 1;
	}

	/* at_fdcwd token: relative open through it must land in cwd */
	auto const cwd{__wine_unix_at_fdcwd()};
	if (cwd == 0)
	{
		return 1;
	}
	char const rel[] = "fast_io_winetest_atfd.txt";
	auto rp{__wine_unix_openat_returns_status(cwd, rel, sizeof(rel) - 1,
											  __WINE_UNIX_O_WRONLY | __WINE_UNIX_O_CREAT | __WINE_UNIX_O_TRUNC, 0644)};
	if (rp.status != __WINE_UNIX_ERRNO_SUCCESS || rp.host_fd == 0)
	{
		return 1;
	}
	if (__wine_unix_close_returns_status(rp.host_fd) != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return 1;
	}
	return 0;
}
