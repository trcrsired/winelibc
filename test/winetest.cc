/*
PE-side proof of concept for the __wine_unix contract.

Build (no wine toolchain):
	clang++ --config=$HOME/herbcfgs/x86_64-windows-msvc.cfg -o winetest.exe src/winetest.cc \
		-I../fast_io/src/__wine_unix/include -lntdll

Run:
	WINEDLLPATH=/home/cqwrteur/libraries/fast_io_kilo/winelibc wine winetest.exe
*/

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
	__declspec(dllimport) int32_t __stdcall LdrLoadDll(char16_t const *path, uint32_t *characteristics,
													 __nt_unicode_string *name, void **handle) noexcept;
}

static constexpr uint32_t MemoryWineLoadUnixLibByName{1002};
static constexpr uint32_t MemoryWineUnloadUnixLib{1004};

static __nt_unicode_string unicode_string(char16_t *s, size_t n)
{
	return {static_cast<uint16_t>(n * sizeof(char16_t)),
			static_cast<uint16_t>(n * sizeof(char16_t) + sizeof(char16_t)), s};
}

/*
The unixcall table plus how each call reaches it. On wine the funcs pointer
comes from NtQueryVirtualMemory and calls must cross through
__wine_unix_call_dispatcher. On windows (or when forced with "nt") the same
table is imported from winelibc_nt.dll and entries are called directly.
*/
static __wine_unixlib_entry_t const *funcs;
static __wine_unix_call_dispatcher_t dispatcher;

static int32_t unixcall(unsigned int code, void *args)
{
	if (dispatcher != nullptr)
	{
		return dispatcher(reinterpret_cast<__wine_unixlib_handle_t>(funcs), code, args);
	}
	return funcs[code](args);
}

int main(int argc, char **argv)
{
	bool const force_nt{argc > 1 && std::strcmp(argv[1], "nt") == 0};

	/* 1. ntdll module handle */
	char16_t ntdll_name[] = u"ntdll.dll";
	auto ntdll_us{unicode_string(ntdll_name, 12)};
	void *ntdll{};
	auto st{LdrGetDllHandle(nullptr, nullptr, &ntdll_us, &ntdll)};
	if (st || !ntdll)
	{
		return 1;
	}

	if (!force_nt)
	{
		/*
		2a. __wine_unix_call_dispatcher is a data export: a variable in ntdll.dll
		    holding the unix-side dispatcher address. LdrGetProcedureAddress gives
		    us the variable's address; dereference once for the callable pointer.
		*/
		char disp_name[] = "__wine_unix_call_dispatcher";
		__nt_ansi_string disp_as{static_cast<uint16_t>(sizeof(disp_name) - 1),
								 static_cast<uint16_t>(sizeof(disp_name)), disp_name};
		void *disp_var{};
		st = LdrGetProcedureAddress(ntdll, &disp_as, 0, &disp_var);
		if (!st && disp_var)
		{
			dispatcher = *static_cast<__wine_unix_call_dispatcher_t *>(disp_var);
		}
	}

	if (!force_nt && dispatcher != nullptr)
	{
		/* 3a. wine path: load the host unixlib by name (WINEDLLPATH dirs + wine's own lib dir) */
		char16_t lib_name[] = u"fast_io_wine";
		auto lib_us{unicode_string(lib_name, 12)};
		uint64_t res[2]{};
		size_t reslen{};
		st = NtQueryVirtualMemory(reinterpret_cast<void *>(static_cast<uintptr_t>(-1)), &lib_us,
								  MemoryWineLoadUnixLibByName, res, sizeof(res), &reslen);
		if (!st && res[1])
		{
			funcs = reinterpret_cast<__wine_unixlib_entry_t const *>(static_cast<uintptr_t>(res[1]));
		}
	}

	if (funcs == nullptr)
	{
		/*
		3b. no dispatcher (real windows, or forced): import the same table from
		    winelibc_nt.dll and call entries directly — same ABI, same binary.
		*/
		char16_t emu_name[] = u"winelibc_nt";
		auto emu_us{unicode_string(emu_name, 11)};
		void *emu{};
		st = LdrLoadDll(nullptr, nullptr, &emu_us, &emu);
		if (st || !emu)
		{
			return 1;
		}
		char tbl_name[] = "__wine_unix_call_funcs";
		__nt_ansi_string tbl_as{static_cast<uint16_t>(sizeof(tbl_name) - 1),
								static_cast<uint16_t>(sizeof(tbl_name)), tbl_name};
		void *tbl_var{};
		st = LdrGetProcedureAddress(emu, &tbl_as, 0, &tbl_var);
		if (st || !tbl_var)
		{
			return 1;
		}
		funcs = static_cast<__wine_unixlib_entry_t const *>(tbl_var);
	}

	/* 4. openat(AT_FDCWD, "/tmp/fast_io_winetest.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) */
	char const path[] = "/tmp/fast_io_winetest.txt";
	__wine_unix_openat_params_t op{};
	op.filename = path;
	op.filenamelen = sizeof(path) - 1;
	op.flags = __WINE_UNIX_O_WRONLY | __WINE_UNIX_O_CREAT | __WINE_UNIX_O_TRUNC;
	op.mode = 0644;
	st = unixcall(__wine_unix_openat, &op);
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
	st = unixcall(__wine_unix_writev, &wp);
	if (st || wp.total != 16)
	{
		return 1;
	}

	/* 6. close, reopen, readv-verify the bytes round-trip */
	__wine_unix_close_params_t cp{op.host_fd};
	st = unixcall(__wine_unix_close, &cp);
	if (st)
	{
		return 1;
	}

	op.flags = __WINE_UNIX_O_RDONLY;
	op.host_fd = 0;
	st = unixcall(__wine_unix_openat, &op);
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
	st = unixcall(__wine_unix_readv, &rp);
	cp.host_fd = op.host_fd;
	st = unixcall(__wine_unix_close, &cp);
	if (st || rp.total != 16 || std::memcmp(buf, "hello unix side\n", 16))
	{
		return 1;
	}
	return 0;
}
