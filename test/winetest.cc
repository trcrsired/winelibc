/*
PE-side proof of concept for the __wine_unix contract.

Build (no wine toolchain):
	clang++ --config=$HOME/herbcfgs/x86_64-windows-msvc.cfg -o winetest.exe src/winetest.cc \
		-I../fast_io/src/__wine_unix/include -lntdll

Run:
	WINEDLLPATH=/home/cqwrteur/libraries/fast_io_kilo/winelibc wine winetest.exe
*/

#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstring>

#include <__wine_unix/__wine_unix.h>
#include <__wine_unix/__wine_unix_errno.h>
#include <__wine_unix/__wine_unix_fcntl.h>

struct __nt_unicode_string
{
	uint16_t Length;
	uint16_t MaximumLength;
	char16_t *Buffer;
};

struct __nt_ansi_string
{
	uint16_t Length;
	uint16_t MaximumLength;
	char *Buffer;
};

extern "C"
{
	__declspec(dllimport) int32_t __stdcall NtQueryVirtualMemory(void *process, void const *addr,
															   uint32_t info_class, void *buffer, size_t len,
															   size_t *res_len) noexcept;
	__declspec(dllimport) int32_t __stdcall LdrGetDllHandle(char16_t const *path, uint32_t *characteristics,
														  __nt_unicode_string *name, void **handle) noexcept;
	__declspec(dllimport) int32_t __stdcall LdrGetProcedureAddress(void *handle, __nt_ansi_string const *name,
																 uint32_t ordinal, void **proc) noexcept;
}

static constexpr uint32_t MemoryWineLoadUnixLibByName{1002};
static constexpr uint32_t MemoryWineUnloadUnixLib{1004};

static __nt_unicode_string unicode_string(char16_t *s, size_t n)
{
	return {static_cast<uint16_t>(n * sizeof(char16_t)),
			static_cast<uint16_t>(n * sizeof(char16_t) + sizeof(char16_t)), s};
}

int main()
{
	/* 1. ntdll module handle */
	char16_t ntdll_name[] = u"ntdll.dll";
	auto ntdll_us{unicode_string(ntdll_name, 12)};
	void *ntdll{};
	auto st{LdrGetDllHandle(nullptr, nullptr, &ntdll_us, &ntdll)};
	printf("LdrGetDllHandle: %lx %p\n", (unsigned long)(uint32_t)st, ntdll);
	if (st || !ntdll)
	{
		return 1;
	}

	/*
	2. __wine_unix_call_dispatcher is a data export: a variable in ntdll.dll
	   holding the unix-side dispatcher address. LdrGetProcedureAddress gives us
	   the variable's address; dereference once for the callable pointer.
	*/
	char disp_name[] = "__wine_unix_call_dispatcher";
	__nt_ansi_string disp_as{static_cast<uint16_t>(sizeof(disp_name) - 1),
							 static_cast<uint16_t>(sizeof(disp_name)), disp_name};
	void *disp_var{};
	st = LdrGetProcedureAddress(ntdll, &disp_as, 0, &disp_var);
	printf("LdrGetProcedureAddress: %lx %p\n", (unsigned long)(uint32_t)st, disp_var);
	if (st || !disp_var)
	{
		return 1;
	}
	auto dispatcher{*static_cast<__wine_unix_call_dispatcher_t *>(disp_var)};
	printf("dispatcher: %p\n", reinterpret_cast<void *>(dispatcher));
	if (!dispatcher)
	{
		return 1;
	}

	/* 3. load our unixlib by name (searches WINEDLLPATH dirs + wine's own lib dir) */
	char16_t lib_name[] = u"fast_io_wine";
	auto lib_us{unicode_string(lib_name, 12)};
	uint64_t res[2]{};
	size_t reslen{};
	st = NtQueryVirtualMemory(reinterpret_cast<void *>(static_cast<uintptr_t>(-1)), &lib_us,
							  MemoryWineLoadUnixLibByName, res, sizeof(res), &reslen);
	printf("NtQueryVirtualMemory(load): %lx module=%llx funcs=%llx\n", (unsigned long)(uint32_t)st,
		   (unsigned long long)res[0], (unsigned long long)res[1]);
	if (st || !res[1])
	{
		return 1;
	}
	auto const unixlib{static_cast<__wine_unixlib_handle_t>(res[1])};

	/* 4. openat(AT_FDCWD, "/tmp/fast_io_winetest.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) */
	char const path[] = "/tmp/fast_io_winetest.txt";
	__wine_unix_openat_params_t op{};
	op.filename = path;
	op.filenamelen = sizeof(path) - 1;
	op.flags = __WINE_UNIX_O_WRONLY | __WINE_UNIX_O_CREAT | __WINE_UNIX_O_TRUNC;
	op.mode = 0644;
	st = dispatcher(unixlib, __wine_unix_openat, &op);
	printf("openat: %ld host_fd=%llu\n", (long)st, (unsigned long long)op.host_fd);
	if (st || !op.host_fd)
	{
		return 1;
	}

	/* 5. writev 3 iovecs */
	char const a[] = "hello ";
	char const b[] = "unix ";
	char const c[] = "side\n";
	__wine_unix_iovec_t iov[3]{{a, 6}, {b, 5}, {c, 5}};
	__wine_unix_readwritev_params_t wp{};
	wp.host_fd = op.host_fd;
	wp.iovs = iov;
	wp.iovsize = 3;
	st = dispatcher(unixlib, __wine_unix_writev, &wp);
	printf("writev: %ld total=%llu baseindex=%llu index=%llu\n", (long)st, (unsigned long long)wp.total,
		   (unsigned long long)wp.baseindex, (unsigned long long)wp.index);
	if (st || wp.total != 16)
	{
		return 1;
	}

	/* 6. close, reopen, readv-verify the bytes round-trip */
	__wine_unix_close_params_t cp{op.host_fd};
	st = dispatcher(unixlib, __wine_unix_close, &cp);
	printf("close: %ld\n", (long)st);
	if (st)
	{
		return 1;
	}

	op.flags = __WINE_UNIX_O_RDONLY;
	op.host_fd = 0;
	st = dispatcher(unixlib, __wine_unix_openat, &op);
	printf("openat(r): %ld host_fd=%llu\n", (long)st, (unsigned long long)op.host_fd);
	if (st || !op.host_fd)
	{
		return 1;
	}
	char buf[32]{};
	__wine_unix_iovec_t riov{buf, sizeof(buf) - 1};
	__wine_unix_readwritev_params_t rp{};
	rp.host_fd = op.host_fd;
	rp.iovs = &riov;
	rp.iovsize = 1;
	st = dispatcher(unixlib, __wine_unix_readv, &rp);
	printf("readv: %ld total=%llu data=%.*s", (long)st, (unsigned long long)rp.total,
		   (int)(rp.total ? rp.total : 0), buf);
	cp.host_fd = op.host_fd;
	st = dispatcher(unixlib, __wine_unix_close, &cp);
	if (st || rp.total != 16 || std::memcmp(buf, "hello unix side\n", 16))
	{
		printf("VERIFY FAILED\n");
		return 1;
	}
	printf("ALL PASS\n");
	return 0;
}
