/*
fast_io wine file test: print "hello world" through fast_io's wine layer
(include/fast_io_hosted/platforms/wine/wine.h) over the wineunix.dll api.

Same exe runs against either sibling dll:

	unixcall/fast_io_winetest.exe -> unixcall wineunix.dll -> libwineunix.so -> libc
	nt/fast_io_winetest.exe       -> nt wineunix.dll -> ntdll

wine_stdout() resolves the right host_fd either way: unix fd + 1 through the
unixcall impl, the process's StandardOutput handle through the nt impl.

Silent: exit status is the only signal (plus the printed line itself).
*/

#include <cstddef>
#include <cstring>

#include <fast_io.h>
#include <fast_io_hosted/platforms/wine/wine.h>

#include <__wine_unix/__wine_unix.h>

int main()
{
	auto wiob{::fast_io::wine_stdout()};
	if (!wiob)
	{
		return 1;
	}
	::fast_io::println(wiob, "hello world");

	/* file round-trip through the same layer */
	char const path[] = "/tmp/fast_io_wine_hello.txt";
	{
		::fast_io::wine_file wf(path, ::fast_io::open_mode::out);
		::fast_io::println(wf, "hello world");
	}
	{
		::fast_io::wine_file wf(path, ::fast_io::open_mode::in);
		char buf[32]{};
		::fast_io::operations::read_all_bytes(
			wf, reinterpret_cast<::std::byte *>(buf), reinterpret_cast<::std::byte *>(buf) + 12);
		if (::std::memcmp(buf, "hello world\n", 12) != 0)
		{
			return 1;
		}
	}
	return 0;
}
