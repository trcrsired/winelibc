#pragma once
/*
Internal contract between wineunix.dll and libwineunix.so — not part of the
user-facing api (that is __wine_unix.h).

Nothing is called directly across the PE <-> unix boundary. Every host libc call
is wrapped as a unixcall:

	__wine_unix_status_t (*)(void *args)

where args points at a "params" struct. Inputs go in, outputs are written back
through the struct, and the return value is the status (0 = success, otherwise
an errno value from __wine_unix_errno.h).

The params struct is the real ABI contract between the two compilers (mingw/MSVC
PE vs host clang/gcc), so its layout is frozen here. For wow64 (a 32-bit PE on a
64-bit host) we also define *_params32 variants, explicitly packed so every
toolchain agrees on the layout; the 64-bit unixlib's
__wine_unix_call_wow64_funcs[] wrappers marshal between them.

On the PE side the call goes through ntdll's __wine_unix_call_dispatcher, which
does the whole context save (registers, stack, TLS) before entering the unixlib.
See the wine source: include/wine/unixlib.h and dlls/ntdll/unix/signal_x86_64.c
(__wine_unix_call_dispatcher). The unixlib is loaded by name with
NtQueryVirtualMemory(GetCurrentProcess(), &name, 1002, ...) which does a plain
dlopen of the .so and dlsym's "__wine_unix_call_funcs" (see
dlls/ntdll/unix/virtual.c).
*/

#include <__wine_unix/__wine_unix.h>

#if defined(_WIN32) && !defined(__WINE__) || defined(__CYGWIN__)
#define __WINE_UNIX_DEFAULTCALL __stdcall
#define __WINE_UNIX_DLLEXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define __WINE_UNIX_DEFAULTCALL
#define __WINE_UNIX_DLLEXPORT __attribute__((visibility("default")))
#else
#define __WINE_UNIX_DEFAULTCALL
#define __WINE_UNIX_DLLEXPORT
#endif

#ifdef __cplusplus
extern "C"
{
#endif

	/* 32-bit pointer value, used to carry wow64 pointers across the boundary. */
	typedef uint32_t __wine_unix_ptr32_t;

	/* opaque handle to the unixlib call table. always 64-bit so it can cross wow64. */
	typedef uint_least64_t __wine_unixlib_handle_t;

	/* every unixcall has this signature; args always points at a params struct. */
	typedef __wine_unix_status_t (*__wine_unixlib_entry_t)(void *args);

	/*
	Call codes. The order MUST match the __wine_unix_call_funcs and
	__wine_unix_call_wow64_funcs tables in unixhost.cc.
	*/
	enum __wine_unix_calls
	{
		__wine_unix_call_host_fd_to_unix_fd,
		__wine_unix_call_unix_fd_to_host_fd,
		__wine_unix_call_host_fd_to_nt_handle,
		__wine_unix_call_nt_handle_to_host_fd,
		__wine_unix_call_openat,
		__wine_unix_call_close,
		__wine_unix_call_writev,
		__wine_unix_call_readv,
		__wine_unix_call_pwritev,
		__wine_unix_call_preadv,
		__wine_unix_call_write,
		__wine_unix_call_read,
		__wine_unix_call_get_std_host_fd,
		__wine_unix_call_at_fdcwd,
		__wine_unix_call_funcs_count,
	};

	typedef struct
	{
		__wine_host_fd_t host_fd;
		int unix_fd; /* output */
	} __wine_unix_host_fd_to_unix_fd_params;

	typedef struct
	{
		int unix_fd;
		__wine_host_fd_t host_fd; /* output */
	} __wine_unix_unix_fd_to_host_fd_params;

	typedef struct
	{
		__wine_host_fd_t host_fd;
		ptrdiff_t handle; /* output */
	} __wine_unix_host_fd_to_nt_handle_params;

	typedef struct
	{
		ptrdiff_t handle;
		__wine_host_fd_t host_fd; /* output */
	} __wine_unix_nt_handle_to_host_fd_params;

	typedef struct
	{
		__wine_host_fd_t host_dirfd; /* 0 means AT_FDCWD */
		char const *filename;
		size_t filenamelen;
		__wine_host_flags_t flags;
		__wine_host_mode_t mode;
		__wine_host_fd_t host_fd; /* output */
	} __wine_unix_openat_params;

	typedef struct
	{
		__wine_host_fd_t host_fd;
	} __wine_unix_close_params;

	typedef struct
	{
		__wine_host_fd_t host_fd;
		__wine_unix_iovec_t const *iovs;
		size_t iovsize;
		size_t total;     /* output: bytes transferred */
		size_t baseindex; /* output: iovec index the transfer stopped at */
		size_t index;     /* output: bytes consumed within that iovec */
	} __wine_unix_readwritev_params;

	typedef struct
	{
		__wine_host_fd_t host_fd;
		__wine_unix_iovec_t const *iovs;
		size_t iovsize;
		__wine_off_t offset;
		size_t total;     /* output */
		size_t baseindex; /* output */
		size_t index;     /* output */
	} __wine_unix_preadwritev_params;

	typedef struct
	{
		int which;                /* 0 stdin, 1 stdout, 2 stderr */
		__wine_host_fd_t host_fd; /* output */
	} __wine_unix_get_std_host_fd_params;

	typedef struct
	{
		/*
		impl-defined at_fdcwd token:
		  unixcall impl: AT_FDCWD encoded as host_fd (unix_fd + 1)
		  nt impl:       -3, fast_io's nt_at_fdcwd sentinel
		*/
		__wine_host_fd_t host_fd; /* output */
	} __wine_unix_at_fdcwd_params;

	typedef struct
	{
		__wine_host_fd_t host_fd;
		void *buf; /* write casts it to void const * */
		size_t len;
		size_t total; /* output: bytes transferred */
	} __wine_unix_readwrite_params;

	/*
	wow64 (32-bit PE on a 64-bit host) variants. Explicitly packed so the layout
	is identical under MSVC x86, mingw x86 and the 64-bit unixlib reader.
	*/
#pragma pack(push, 1)
	typedef struct
	{
		__wine_unix_ptr32_t host_fd;
		int32_t unix_fd;
	} __wine_unix_host_fd_to_unix_fd_params32;

	typedef struct
	{
		int32_t unix_fd;
		__wine_unix_ptr32_t host_fd;
	} __wine_unix_unix_fd_to_host_fd_params32;

	typedef struct
	{
		__wine_unix_ptr32_t host_fd;
		int32_t handle;
	} __wine_unix_host_fd_to_nt_handle_params32;

	typedef struct
	{
		int32_t handle;
		__wine_unix_ptr32_t host_fd;
	} __wine_unix_nt_handle_to_host_fd_params32;

	typedef struct
	{
		__wine_unix_ptr32_t host_dirfd;
		__wine_unix_ptr32_t filename;
		__wine_unix_ptr32_t filenamelen;
		__wine_unix_ptr32_t flags;
		__wine_unix_ptr32_t mode;
		__wine_unix_ptr32_t host_fd;
	} __wine_unix_openat_params32;

	typedef struct
	{
		__wine_unix_ptr32_t host_fd;
	} __wine_unix_close_params32;

	typedef struct
	{
		__wine_unix_ptr32_t host_fd;
		__wine_unix_ptr32_t iovs;
		uint32_t iovsize;
		uint32_t total;
		uint32_t baseindex;
		uint32_t index;
	} __wine_unix_readwritev_params32;

	typedef struct
	{
		__wine_unix_ptr32_t host_fd;
		__wine_unix_ptr32_t iovs;
		uint32_t iovsize;
		__wine_off_t offset;
		uint32_t total;
		uint32_t baseindex;
		uint32_t index;
	} __wine_unix_preadwritev_params32;

	typedef struct
	{
		int32_t which;
		__wine_unix_ptr32_t host_fd;
	} __wine_unix_get_std_host_fd_params32;

	typedef struct
	{
		__wine_unix_ptr32_t host_fd;
	} __wine_unix_at_fdcwd_params32;

	typedef struct
	{
		__wine_unix_ptr32_t host_fd;
		__wine_unix_ptr32_t buf;
		uint32_t len;
		uint32_t total;
	} __wine_unix_readwrite_params32;
#pragma pack(pop)

	/* arch-selected params types: what a given side actually builds/passes. */
#if INTPTR_MAX < INT64_MAX
	typedef __wine_unix_host_fd_to_unix_fd_params32 __wine_unix_host_fd_to_unix_fd_params_t;
	typedef __wine_unix_unix_fd_to_host_fd_params32 __wine_unix_unix_fd_to_host_fd_params_t;
	typedef __wine_unix_host_fd_to_nt_handle_params32 __wine_unix_host_fd_to_nt_handle_params_t;
	typedef __wine_unix_nt_handle_to_host_fd_params32 __wine_unix_nt_handle_to_host_fd_params_t;
	typedef __wine_unix_openat_params32 __wine_unix_openat_params_t;
	typedef __wine_unix_close_params32 __wine_unix_close_params_t;
	typedef __wine_unix_readwritev_params32 __wine_unix_readwritev_params_t;
	typedef __wine_unix_preadwritev_params32 __wine_unix_preadwritev_params_t;
	typedef __wine_unix_get_std_host_fd_params32 __wine_unix_get_std_host_fd_params_t;
	typedef __wine_unix_at_fdcwd_params32 __wine_unix_at_fdcwd_params_t;
	typedef __wine_unix_readwrite_params32 __wine_unix_readwrite_params_t;
#else
typedef __wine_unix_host_fd_to_unix_fd_params __wine_unix_host_fd_to_unix_fd_params_t;
typedef __wine_unix_unix_fd_to_host_fd_params __wine_unix_unix_fd_to_host_fd_params_t;
typedef __wine_unix_host_fd_to_nt_handle_params __wine_unix_host_fd_to_nt_handle_params_t;
typedef __wine_unix_nt_handle_to_host_fd_params __wine_unix_nt_handle_to_host_fd_params_t;
typedef __wine_unix_openat_params __wine_unix_openat_params_t;
typedef __wine_unix_close_params __wine_unix_close_params_t;
typedef __wine_unix_readwritev_params __wine_unix_readwritev_params_t;
typedef __wine_unix_preadwritev_params __wine_unix_preadwritev_params_t;
typedef __wine_unix_get_std_host_fd_params __wine_unix_get_std_host_fd_params_t;
typedef __wine_unix_at_fdcwd_params __wine_unix_at_fdcwd_params_t;
typedef __wine_unix_readwrite_params __wine_unix_readwrite_params_t;
#endif

#ifdef WINE_UNIX_LIB
	/* unixlib (unixhost.cc) exports: */
	extern __WINE_UNIX_DLLEXPORT __wine_unixlib_entry_t const __wine_unix_call_funcs[];
	__WINE_UNIX_DLLEXPORT __wine_unix_status_t __wine_unix_lib_init(void) __WINE_UNIX_NOEXCEPT;
#if INTPTR_MAX >= INT64_MAX
	extern __WINE_UNIX_DLLEXPORT __wine_unixlib_entry_t const __wine_unix_call_wow64_funcs[];
#endif
#else
/*
PE side: ntdll's __wine_unix_call_dispatcher (a data export holding the
dispatcher address). The "unixlib handle" is the loaded unixlib's
__wine_unix_call_funcs table pointer.
*/
typedef __wine_unix_status_t(__WINE_UNIX_DEFAULTCALL *__wine_unix_call_dispatcher_t)(__wine_unixlib_handle_t,
																					 unsigned int, void *);
#endif

#ifdef __cplusplus
}
#endif
