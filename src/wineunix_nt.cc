/*
wineunix.dll (nt implementation) — built for real windows.

	clang++ --config=$HOME/herbcfgs/x86_64-windows-msvc.cfg -fherbceptions -shared \
		-o wineunix.dll src/wineunix_nt.cc -Iinclude -lntdll

Same exports as the unixcall wineunix.dll, but every call forwards straight to
ntdll — no libwineunix.so, no dispatcher, no params marshalling. Swapping this
dll in is all it takes for a binary built against __wine_unix.h to run on real
windows.
*/

#define __WINE_UNIX_DLL_BUILD

#include <__wine_unix/__wine_unix.h>

#include "ntdll_imports.h"

#include <cstdint>
#include <cstddef>
#include <type_traits>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace winelibc_nt
{

/* TEB lives in x18 on arm64 (fast_io fast_io_nt_current_teb). register-asm
   variables need external linkage, so this sits outside the anon namespace. */
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__aarch64__) || defined(__arm64ec__))
register char *nt_current_teb_reg __asm__("x18");
#endif

namespace
{

inline constexpr uint32_t nt_status_success{0};
inline constexpr uint32_t obj_case_insensitive{0x40};

inline constexpr uint32_t file_read_data{0x0001};
inline constexpr uint32_t file_write_data{0x0002};
inline constexpr uint32_t file_append_data{0x0004};
inline constexpr uint32_t file_read_attributes{0x0080};
inline constexpr uint32_t file_write_attributes{0x0100};
inline constexpr uint32_t delete_access{0x00010000};
inline constexpr uint32_t synchronize{0x00100000};

inline constexpr uint32_t file_share_read{0x00000001};
inline constexpr uint32_t file_share_write{0x00000002};
inline constexpr uint32_t file_share_delete{0x00000004};

inline constexpr uint32_t file_open{0x00000001};
inline constexpr uint32_t file_create{0x00000002};
inline constexpr uint32_t file_open_if{0x00000003};
inline constexpr uint32_t file_overwrite{0x00000004};
inline constexpr uint32_t file_overwrite_if{0x00000005};

inline constexpr uint32_t file_directory_file{0x00000001};
inline constexpr uint32_t file_write_through{0x00000002};
inline constexpr uint32_t file_no_intermediate_buffering{0x00000008};
inline constexpr uint32_t file_synchronous_io_nonalert{0x00000020};
inline constexpr uint32_t file_non_directory_file{0x00000040};
inline constexpr uint32_t file_delete_on_close{0x00001000};
inline constexpr uint32_t file_open_reparse_point{0x00200000};
inline constexpr uint32_t file_open_for_backup_intent{0x00004000};

inline constexpr uint32_t file_attribute_normal{0x00000080};

inline constexpr uint32_t status_object_name_not_found{0xC0000034};
inline constexpr uint32_t status_object_path_not_found{0xC000003A};
inline constexpr uint32_t status_object_name_collision{0xC0000035};
inline constexpr uint32_t status_access_denied{0xC0000022};
inline constexpr uint32_t status_sharing_violation{0xC0000043};
inline constexpr uint32_t status_invalid_parameter{0xC000000D};
inline constexpr uint32_t status_invalid_handle{0xC0000008};
inline constexpr uint32_t status_not_a_directory{0xC0000103};
inline constexpr uint32_t status_file_is_a_directory{0xC00000BA};
inline constexpr uint32_t status_name_too_long{0xC0000106};
inline constexpr uint32_t status_disk_full{0xC000007F};
inline constexpr uint32_t status_insufficient_resources{0xC000009A};
/* read-side EOF statuses: a short/empty result, not an error */
inline constexpr uint32_t status_end_of_file{0xC0000011};
inline constexpr uint32_t status_pipe_broken{0xC000014B};

inline __wine_unix_status_t ntstatus_to_wine_errno(int32_t status) noexcept
{
	switch (static_cast<uint32_t>(status))
	{
	case nt_status_success:
		return __WINE_UNIX_ERRNO_SUCCESS;
	case status_object_name_not_found:
		return __WINE_UNIX_ERRNO_ENOENT;
	case status_object_path_not_found:
	case status_not_a_directory:
		return __WINE_UNIX_ERRNO_ENOTDIR;
	case status_file_is_a_directory:
		return __WINE_UNIX_ERRNO_EISDIR;
	case status_object_name_collision:
		return __WINE_UNIX_ERRNO_EEXIST;
	case status_access_denied:
		return __WINE_UNIX_ERRNO_EACCES;
	case status_sharing_violation:
		return __WINE_UNIX_ERRNO_EBUSY;
	case status_invalid_parameter:
		return __WINE_UNIX_ERRNO_EINVAL;
	case status_invalid_handle:
		return __WINE_UNIX_ERRNO_EBADF;
	case status_name_too_long:
		return __WINE_UNIX_ERRNO_ENAMETOOLONG;
	case status_disk_full:
		return __WINE_UNIX_ERRNO_ENOSPC;
	case status_insufficient_resources:
		return __WINE_UNIX_ERRNO_ENOMEM;
	default:
		return __WINE_UNIX_ERRNO_EIO;
	}
}

/*
host_fd encoding: on the unix side it is unix_fd + 1; on the NT side it is the
raw windows HANDLE value (a valid HANDLE is never 0, so 0 still means "no fd").
*/
inline __wine_unix_status_t host_fd_to_handle(__wine_host_fd_t host_fd, void *&handle) noexcept
{
	handle = reinterpret_cast<void *>(static_cast<::std::uintptr_t>(host_fd));
	if (handle == nullptr)
	{
		return __WINE_UNIX_ERRNO_EBADF;
	}
	return __WINE_UNIX_ERRNO_SUCCESS;
}

inline __wine_host_fd_t handle_to_host_fd(void *handle) noexcept
{
	return static_cast<__wine_host_fd_t>(reinterpret_cast<::std::uintptr_t>(handle));
}

/*
The NT side has no unix path namespace; emulate it by mapping "/" onto a drive
root: Z: under wine (its unix-filesystem drive), C: elsewhere. Detected once by
checking whether ntdll exports wine_get_version.
*/
inline char16_t unix_root_drive() noexcept
{
	static char16_t const drive{[]() noexcept -> char16_t {
		char16_t ntdll_name[] = u"ntdll.dll";
		unicode_string us{static_cast<uint16_t>(24), static_cast<uint16_t>(26), ntdll_name};
		void *ntdll{};
		if (ntdll_LdrGetDllHandle(nullptr, nullptr, &us, &ntdll))
		{
			return u'C';
		}
		char probe[] = "wine_get_version";
		ansi_string as{static_cast<uint16_t>(sizeof(probe) - 1), static_cast<uint16_t>(sizeof(probe)), probe};
		void *proc{};
		if (ntdll_LdrGetProcedureAddress(ntdll, &as, 0, &proc) || proc == nullptr)
		{
			return u'C';
		}
		return u'Z';
	}()};
	return drive;
}

inline constexpr ::std::size_t nt_path_max{4096};

/* unix relative path -> NT path relative to RootDirectory. returns wchar count or 0 */
inline ::std::size_t unix_rel_to_nt(char const *filename, ::std::size_t filenamelen, char16_t *out,
									::std::size_t outcap) noexcept
{
	::std::size_t n{};
	while (n < filenamelen && (filename[n] == u8'/' || filename[n] == u8'\\'))
	{
		++n; /* a name relative to a dirfd cannot start with a separator */
	}
	filename += n;
	filenamelen -= n;
	if (filenamelen == 0 || outcap < filenamelen)
	{
		return 0;
	}
	for (::std::size_t i{}; i != filenamelen; ++i)
	{
		char const ch{filename[i]};
		if (ch == 0 || (ch & 0x80))
		{
			return 0;
		}
		out[i] = static_cast<char16_t>(ch == u8'/' ? u8'\\' : ch);
	}
	out[filenamelen] = 0;
	return filenamelen;
}

/* unix absolute path -> \??\<DRV>:\path. returns wchar count or 0 */
inline ::std::size_t unix_abs_to_nt(char const *filename, ::std::size_t filenamelen, char16_t *out,
									::std::size_t outcap) noexcept
{
	constexpr char16_t prefix[] = u"\\??\\X:\\";
	if (filenamelen == 0 || filename[0] != u8'/' || outcap < filenamelen + 8)
	{
		return 0;
	}
	::std::size_t pos{};
	for (; pos != 4; ++pos)
	{
		out[pos] = prefix[pos];
	}
	out[pos++] = unix_root_drive();
	out[pos++] = u':';
	for (::std::size_t i{}; i != filenamelen; ++i)
	{
		char const ch{filename[i]};
		if (ch == 0 || (ch & 0x80))
		{
			return 0;
		}
		out[pos] = static_cast<char16_t>(ch == u8'/' ? u8'\\' : ch);
		++pos;
	}
	out[pos] = 0;
	return pos;
}

__wine_unix_unix_fd_status_t nt_host_fd_to_unix_fd(__wine_host_fd_t host_fd) noexcept
{
	void *handle{};
	if (auto const err{host_fd_to_handle(host_fd, handle)}; err)
	{
		return {err, -1};
	}
	auto const v{reinterpret_cast<::std::uintptr_t>(handle)};
	if (static_cast<::std::uintptr_t>(static_cast<int>(v)) != v)
	{
		return {__WINE_UNIX_ERRNO_EBADF, -1};
	}
	return {__WINE_UNIX_ERRNO_SUCCESS, static_cast<int>(v)};
}

__wine_unix_host_fd_status_t nt_unix_fd_to_host_fd(int unix_fd) noexcept
{
	return {__WINE_UNIX_ERRNO_SUCCESS,
			unix_fd < 0 ? 0 : static_cast<__wine_host_fd_t>(unix_fd)};
}

/*
conversions transfer ownership: on the nt impl a host_fd already IS the HANDLE,
so both directions are just re-encodings — no dup. (The unixcall impl consumes
too: the wineserver keeps its own fd via SCM_RIGHTS, and handle->fd closes the
source handle on the PE side.)
*/

__wine_unix_nt_handle_status_t nt_host_fd_to_nt_handle(__wine_host_fd_t host_fd) noexcept
{
	void *handle{};
	auto const err{host_fd_to_handle(host_fd, handle)};
	return {err, static_cast<ptrdiff_t>(reinterpret_cast<::std::uintptr_t>(handle))};
}

__wine_unix_host_fd_status_t nt_nt_handle_to_host_fd(ptrdiff_t handle) noexcept
{
	return {__WINE_UNIX_ERRNO_SUCCESS,
			handle_to_host_fd(reinterpret_cast<void *>(static_cast<::std::uintptr_t>(handle)))};
}

/*
nt_get_current_peb, ported from fast_io's nt_preliminary_definition.h for every
arch it supports. ntdll_RtlGetCurrentPeb is the fallback where no direct read exists.
*/
inline void *nt_current_peb() noexcept
{
#if defined(__GNUC__) || defined(__clang__)
#if defined(__aarch64__) || defined(__arm64ec__)
	/* TEB is x18 on arm64; ProcessEnvironmentBlock at 0x60 */
	return *reinterpret_cast<void **>(nt_current_teb_reg + 0x60);
#elif defined(__i386__) || defined(__x86_64__)
	if constexpr (sizeof(::std::size_t) == sizeof(::std::uint_least64_t))
	{
		void *peb;
		__asm__("{movq\t%%gs:0x60, %0|mov\t%0, %%gs:[0x60]}" : "=r"(peb));
		return peb;
	}
	else if constexpr (sizeof(::std::size_t) == sizeof(::std::uint_least32_t))
	{
		void *peb;
		__asm__("{movl\t%%fs:0x30, %0|mov\t%0, %%fs:[0x30]}" : "=r"(peb));
		return peb;
	}
	else
	{
		return ntdll_RtlGetCurrentPeb();
	}
#else
	if constexpr (sizeof(::std::size_t) == sizeof(::std::uint_least32_t))
	{
		char *teb;
		__asm__("MRC p15, 0, %0, c13, c0, 2" : "=r"(teb));
		return *reinterpret_cast<void **>(teb + 0x30);
	}
	else
	{
		return ntdll_RtlGetCurrentPeb();
	}
#endif
#elif defined(_MSC_VER)
#if defined(_M_ARM64) || defined(_M_ARM64EC)
	return *reinterpret_cast<void **>(reinterpret_cast<char *>(__getReg(18)) + 0x60);
#elif defined(_M_AMD64)
	return reinterpret_cast<void *>(__readgsqword(0x60));
#elif defined(_M_IX86)
	return reinterpret_cast<void *>(__readfsdword(0x30));
#else
	return *reinterpret_cast<void **>(
		reinterpret_cast<char *>(_MoveFromCoprocessor(15, 0, 13, 0, 2)) + 0x30);
#endif
#else
	return ntdll_RtlGetCurrentPeb();
#endif
}

/* fast_io rtl_get_process_heap: peb::ProcessHeap — 0x30 on 64-bit, 0x18 on 32-bit */
inline void *process_heap() noexcept
{
	auto *peb{static_cast<char *>(nt_current_peb())};
	return *reinterpret_cast<void **>(
		peb + (sizeof(::std::size_t) == sizeof(::std::uint_least64_t) ? 0x30 : 0x18));
}

/*
PEB->ProcessParameters->CurrentDirectory.Handle: the process's nt cwd
directory handle, RootDirectory for relative opens when the abi asks for
cwd (host_dirfd == 0). curdir::Handle sits at +0x48 inside
rtl_user_process_parameters on 64-bit, +0x2c on 32-bit.
*/
inline void *nt_current_directory_handle() noexcept
{
	constexpr ::std::size_t peb_process_parameters_off{
		sizeof(::std::size_t) == sizeof(::std::uint_least64_t) ? 0x20 : 0x14};
	constexpr ::std::size_t curdir_handle_off{
		sizeof(::std::size_t) == sizeof(::std::uint_least64_t) ? 0x48 : 0x2c};
	auto *pparam{*reinterpret_cast<char **>(static_cast<char *>(nt_current_peb()) + peb_process_parameters_off)};
	return *reinterpret_cast<void **>(pparam + curdir_handle_off);
}

__wine_unix_host_fd_status_t nt_openat(__wine_host_fd_t host_dirfd, char const *filename,
									   ::std::size_t filenamelen, __wine_host_flags_t flags,
									   __wine_host_mode_t mode) noexcept
{
	char16_t ntbfr[nt_path_max];
	unicode_string us{0, static_cast<uint16_t>(sizeof(ntbfr)), ntbfr};
	void *rootdir{};
	::std::size_t wlen{};
	if (filenamelen != 0 && filename[0] == u8'/')
	{
		/* absolute path ignores the dirfd, like openat */
		wlen = unix_abs_to_nt(filename, filenamelen, ntbfr, nt_path_max - 1);
	}
	else
	{
		if (host_dirfd == 0)
		{
			rootdir = nt_current_directory_handle();
		}
		else if (auto const err{host_fd_to_handle(host_dirfd, rootdir)}; err)
		{
			return {err, 0};
		}
		wlen = unix_rel_to_nt(filename, filenamelen, ntbfr, nt_path_max - 1);
	}
	if (wlen == 0)
	{
		return {__WINE_UNIX_ERRNO_ENAMETOOLONG, 0};
	}
	us.Length = static_cast<uint16_t>(wlen * sizeof(char16_t));
	uint32_t access{file_read_attributes | file_write_attributes | synchronize};
	uint32_t options{file_synchronous_io_nonalert};
	uint32_t disposition{file_open};

	if ((flags & __WINE_UNIX_O_DIRECTORY) != 0)
	{
		options |= file_directory_file;
	}
	if ((flags & __WINE_UNIX_O_NOFOLLOW) != 0)
	{
		options |= file_open_reparse_point;
	}
	if ((flags & __WINE_UNIX_O_DIRECT) != 0)
	{
		options |= file_no_intermediate_buffering;
	}
	if ((flags & __WINE_UNIX_O_SYNC) != 0 || (flags & __WINE_UNIX_O_DSYNC) != 0)
	{
		options |= file_write_through;
	}

	bool const append{(flags & __WINE_UNIX_O_APPEND) != 0};
	bool const creat{(flags & __WINE_UNIX_O_CREAT) != 0};
	bool const excl{(flags & __WINE_UNIX_O_EXCL) != 0};
	bool const trunc{(flags & __WINE_UNIX_O_TRUNC) != 0};

	switch (flags & __WINE_UNIX_O_ACCMODE)
	{
	case __WINE_UNIX_O_WRONLY:
		access |= append ? file_append_data : file_write_data;
		break;
	case __WINE_UNIX_O_RDWR:
		access |= file_read_data | (append ? file_append_data : file_write_data);
		break;
	default:
		access |= file_read_data;
		break;
	}

	if (creat && excl)
	{
		disposition = file_create;
	}
	else if (trunc)
	{
		disposition = creat ? file_overwrite_if : file_overwrite;
	}
	else if (creat)
	{
		disposition = file_open_if;
	}

	object_attributes oa{};
	oa.Length = sizeof(oa);
	oa.RootDirectory = rootdir;
	oa.ObjectName = &us;
	oa.Attributes = obj_case_insensitive;

	io_status_block iosb{};
	void *handle{};
	auto const status{ntdll_NtCreateFile(&handle, access, &oa, &iosb, nullptr, file_attribute_normal,
										 file_share_read | file_share_write | file_share_delete, disposition, options,
										 nullptr, 0)};
	if (status != 0)
	{
		return {ntstatus_to_wine_errno(status), 0};
	}
	return {__WINE_UNIX_ERRNO_SUCCESS, handle_to_host_fd(handle)};
}

__wine_unix_status_t nt_close(__wine_host_fd_t host_fd) noexcept
{
	void *handle{};
	if (auto const err{host_fd_to_handle(host_fd, handle)}; err)
	{
		return err;
	}
	return ntstatus_to_wine_errno(ntdll_NtClose(handle));
}

/*
readv/writev carry POSIX read_some/write_some semantics: one ntdll_NtReadFile /
ntdll_NtWriteFile per call, like the single unix syscall they emulate. NT has no
general vectored file I/O (NtReadFileScatter/NtWriteFileGather require
FILE_NO_INTERMEDIATE_BUFFERING handles), so calls with more than one iovec are
gathered/scattered through one process-heap buffer — the ntdll_RtlAllocateHeap
staging fast_io uses for its nt/win32 scatter buffer logic.
*/

/*
fast_io nt_get_stdhandle: PEB->ProcessParameters->Standard{Input,Output,Error}.
ProcessParameters at peb+0x20 (64-bit) / +0x14 (32-bit); inside
rtl_user_process_parameters the std handles sit at +0x20/+0x28/+0x30 (64-bit)
or +0x18/+0x1c/+0x20 (32-bit).
*/
inline void *nt_get_std_handle(int which) noexcept
{
	if (which < 0 || 2 < which)
	{
		return nullptr;
	}
	constexpr ::std::size_t peb_process_parameters_off{
		sizeof(::std::size_t) == sizeof(::std::uint_least64_t) ? 0x20 : 0x14};
	constexpr ::std::size_t standard_input_off{
		sizeof(::std::size_t) == sizeof(::std::uint_least64_t) ? 0x20 : 0x18};
	auto *pparam{*reinterpret_cast<char **>(static_cast<char *>(nt_current_peb()) + peb_process_parameters_off)};
	return *reinterpret_cast<void **>(pparam + standard_input_off +
									  static_cast<::std::size_t>(which) * sizeof(void *));
}

__wine_unix_host_fd_status_t nt_get_std_host_fd(int which) noexcept
{
	return {__WINE_UNIX_ERRNO_SUCCESS, handle_to_host_fd(nt_get_std_handle(which))};
}

struct rwv_buffer
{
	char *ptr{};
	rwv_buffer() noexcept = default;
	rwv_buffer(rwv_buffer const &) = delete;
	rwv_buffer &operator=(rwv_buffer const &) = delete;
	~rwv_buffer()
	{
		if (ptr != nullptr)
		{
			ntdll_RtlFreeHeap(process_heap(), 0, ptr);
		}
	}
	char *allocate(::std::size_t n) noexcept
	{
		ptr = static_cast<char *>(ntdll_RtlAllocateHeap(process_heap(), 0, n));
		return ptr;
	}
};

/* overflow-clamped total iov length */
inline ::std::size_t rwv_total_size(__wine_unix_iovec_t const *iovs, ::std::size_t n) noexcept
{
	::std::size_t total{};
	for (::std::size_t i{}; i != n; ++i)
	{
		auto const ilen{iovs[i].iov_len};
		if (SIZE_MAX - ilen < total)
		{
			break;
		}
		total += ilen;
	}
	return total;
}

inline void rwv_copy_in(__wine_unix_iovec_t const *iovs, ::std::size_t n, char *dst) noexcept
{
	for (::std::size_t i{}; i != n; ++i)
	{
		auto const len{iovs[i].iov_len};
		if (len != 0)
		{
			__builtin_memcpy(dst, iovs[i].iov_base, len);
			dst += len;
		}
	}
}

inline void rwv_copy_out(char const *src, __wine_unix_iovec_t const *iovs, ::std::size_t n,
						 ::std::size_t done) noexcept
{
	for (::std::size_t i{}; i != n && done != 0; ++i)
	{
		auto copied{iovs[i].iov_len};
		if (done < copied)
		{
			copied = done;
		}
		if (copied != 0)
		{
			__builtin_memcpy(const_cast<void *>(iovs[i].iov_base), src, copied);
			src += copied;
			done -= copied;
		}
	}
}

/* splits a flat byte count into {baseindex, index}, like the unix-side split */
inline __wine_unix_rwv_status_t rwv_split(__wine_unix_status_t status, ::std::size_t done,
										  __wine_unix_iovec_t const *iovs, ::std::size_t n) noexcept
{
	::std::size_t baseindex{}, index{}, lastn{done};
	for (; baseindex != n; ++baseindex)
	{
		auto const ilen{iovs[baseindex].iov_len};
		if (lastn < ilen)
		{
			index = lastn;
			break;
		}
		lastn -= ilen;
	}
	return {status, done, baseindex, index};
}

/*
Transfer [buf, buf+len) as a series of ntdll_NtReadFile/ntdll_NtWriteFile calls — Length is
ULONG so each call covers at most 4GiB; the loop keeps going while chunks come
back full and stops at the first short or failed one (POSIX read_some/
write_some). offp is the caller's ByteOffset for the p-variants (nullptr
otherwise) and advances per chunk; nonnegative offsets get the pre-call
overflow check, negative ones (-1/-2 special positions) pass through
unmodified, matching fast_io's thunk.
*/
__wine_unix_status_t nt_transfer(void *handle, void *buf, ::std::size_t len, int64_t *offp,
								 bool write, ::std::size_t &done) noexcept
{
	done = 0;
	auto *first{static_cast<char *>(buf)};
	auto *const last{first + len};
	while (first != last)
	{
		uint32_t const request{static_cast<::std::size_t>(last - first) < 0xFFFFFFFFuz
								   ? static_cast<uint32_t>(last - first)
								   : 0xFFFFFFFFu};
		if (offp != nullptr && 0 <= *offp)
		{
			int64_t nxt;
			if (__builtin_add_overflow(*offp, static_cast<int64_t>(request), __builtin_addressof(nxt)))
			{
				return __WINE_UNIX_ERRNO_EOVERFLOW;
			}
		}
		io_status_block iosb{};
		auto const st{write ? ntdll_NtWriteFile(handle, nullptr, nullptr, nullptr, __builtin_addressof(iosb),
												first, request, offp, nullptr)
							: ntdll_NtReadFile(handle, nullptr, nullptr, nullptr, __builtin_addressof(iosb),
											   first, request, offp, nullptr)};
		if (st != 0)
		{
			auto const ust{static_cast<uint32_t>(st)};
			if (!write && (ust == status_end_of_file || ust == status_pipe_broken))
			{
				return __WINE_UNIX_ERRNO_SUCCESS;
			}
			return ntstatus_to_wine_errno(st);
		}
		auto const transferred{static_cast<::std::size_t>(iosb.Information)};
		done += transferred;
		if (offp != nullptr && 0 <= *offp)
		{
			*offp += static_cast<int64_t>(transferred);
		}
		first += transferred;
		if (transferred < request)
		{
			break;
		}
	}
	return __WINE_UNIX_ERRNO_SUCCESS;
}

__wine_unix_rwv_status_t nt_writev_common(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
										  ::std::size_t iovsize, int64_t *offp) noexcept
{
	if (iovsize == 0)
	{
		return {__WINE_UNIX_ERRNO_SUCCESS, 0, 0, 0};
	}
	void *handle{};
	if (auto const err{host_fd_to_handle(host_fd, handle)}; err)
	{
		return {err, 0, 0, 0};
	}
	::std::size_t const total{rwv_total_size(iovs, iovsize)};
	if (total == 0)
	{
		return {__WINE_UNIX_ERRNO_SUCCESS, 0, iovsize, 0};
	}
	rwv_buffer buffer;
	char const *base{static_cast<char const *>(iovs[0].iov_base)};
	if (iovsize != 1)
	{
		if (buffer.allocate(total) == nullptr)
		{
			return {__WINE_UNIX_ERRNO_ENOMEM, 0, 0, 0};
		}
		rwv_copy_in(iovs, iovsize, buffer.ptr);
		base = buffer.ptr;
	}
	::std::size_t done{};
	auto const st{nt_transfer(handle, const_cast<char *>(base), total, offp, true, done)};
	return rwv_split(st, done, iovs, iovsize);
}

__wine_unix_rwv_status_t nt_readv_common(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
										 ::std::size_t iovsize, int64_t *offp) noexcept
{
	if (iovsize == 0)
	{
		return {__WINE_UNIX_ERRNO_SUCCESS, 0, 0, 0};
	}
	void *handle{};
	if (auto const err{host_fd_to_handle(host_fd, handle)}; err)
	{
		return {err, 0, 0, 0};
	}
	::std::size_t const total{rwv_total_size(iovs, iovsize)};
	if (total == 0)
	{
		return {__WINE_UNIX_ERRNO_SUCCESS, 0, iovsize, 0};
	}
	rwv_buffer buffer;
	char *dst{static_cast<char *>(const_cast<void *>(iovs[0].iov_base))};
	if (iovsize != 1)
	{
		if (buffer.allocate(total) == nullptr)
		{
			return {__WINE_UNIX_ERRNO_ENOMEM, 0, 0, 0};
		}
		dst = buffer.ptr;
	}
	::std::size_t done{};
	auto const st{nt_transfer(handle, dst, total, offp, false, done)};
	if (st != __WINE_UNIX_ERRNO_SUCCESS)
	{
		return {st, 0, 0, 0};
	}
	if (iovsize != 1 && done != 0)
	{
		rwv_copy_out(buffer.ptr, iovs, iovsize, done);
	}
	return rwv_split(__WINE_UNIX_ERRNO_SUCCESS, done, iovs, iovsize);
}

__wine_unix_rwv_status_t nt_writev(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
								   ::std::size_t iovsize) noexcept
{
	return nt_writev_common(host_fd, iovs, iovsize, nullptr);
}

__wine_unix_rwv_status_t nt_readv(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
								  ::std::size_t iovsize) noexcept
{
	return nt_readv_common(host_fd, iovs, iovsize, nullptr);
}

__wine_unix_rwv_status_t nt_pwritev(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									::std::size_t iovsize, __wine_off_t offset) noexcept
{
	auto off{static_cast<int64_t>(offset)};
	return nt_writev_common(host_fd, iovs, iovsize, __builtin_addressof(off));
}

__wine_unix_rwv_status_t nt_preadv(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
								   ::std::size_t iovsize, __wine_off_t offset) noexcept
{
	auto off{static_cast<int64_t>(offset)};
	return nt_readv_common(host_fd, iovs, iovsize, __builtin_addressof(off));
}

__wine_unix_rw_status_t nt_write(__wine_host_fd_t host_fd, void const *buf, ::std::size_t len) noexcept
{
	void *handle{};
	if (auto const err{host_fd_to_handle(host_fd, handle)}; err)
	{
		return {err, 0};
	}
	::std::size_t done{};
	auto const st{nt_transfer(handle, const_cast<void *>(buf), len, nullptr, true, done)};
	return {st, done};
}

__wine_unix_rw_status_t nt_read(__wine_host_fd_t host_fd, void *buf, ::std::size_t len) noexcept
{
	void *handle{};
	if (auto const err{host_fd_to_handle(host_fd, handle)}; err)
	{
		return {err, 0};
	}
	::std::size_t done{};
	auto const st{nt_transfer(handle, buf, len, nullptr, false, done)};
	return {st, done};
}

} // namespace
} // namespace winelibc_nt

#if !defined(__WINE_UNIX_NT_INTERNAL__)

extern "C"
{
	__WINE_UNIX_API __wine_unix_unix_fd_status_t
	__wine_unix_host_fd_to_unix_fd_returns_status(__wine_host_fd_t host_fd) noexcept
	{
		return ::winelibc_nt::nt_host_fd_to_unix_fd(host_fd);
	}

	__WINE_UNIX_API __wine_unix_host_fd_status_t
	__wine_unix_unix_fd_to_host_fd_returns_status(int unix_fd) noexcept
	{
		return ::winelibc_nt::nt_unix_fd_to_host_fd(unix_fd);
	}

	__WINE_UNIX_API __wine_unix_nt_handle_status_t
	__wine_unix_host_fd_to_nt_handle_returns_status(__wine_host_fd_t host_fd) noexcept
	{
		return ::winelibc_nt::nt_host_fd_to_nt_handle(host_fd);
	}

	__WINE_UNIX_API __wine_unix_host_fd_status_t
	__wine_unix_nt_handle_to_host_fd_returns_status(ptrdiff_t handle) noexcept
	{
		return ::winelibc_nt::nt_nt_handle_to_host_fd(handle);
	}

	__WINE_UNIX_API __wine_unix_host_fd_status_t
	__wine_unix_openat_returns_status(__wine_host_fd_t host_dirfd, char const *filename, size_t filenamelen,
									  __wine_host_flags_t flags, __wine_host_mode_t mode) noexcept
	{
		return ::winelibc_nt::nt_openat(host_dirfd, filename, filenamelen, flags, mode);
	}

	__WINE_UNIX_API __wine_unix_status_t __wine_unix_close_returns_status(__wine_host_fd_t host_fd) noexcept
	{
		return ::winelibc_nt::nt_close(host_fd);
	}

	__WINE_UNIX_API __wine_unix_rwv_status_t
	__wine_unix_writev_returns_status(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									  size_t iovsize) noexcept
	{
		return ::winelibc_nt::nt_writev(host_fd, iovs, iovsize);
	}

	__WINE_UNIX_API __wine_unix_rwv_status_t
	__wine_unix_readv_returns_status(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									 size_t iovsize) noexcept
	{
		return ::winelibc_nt::nt_readv(host_fd, iovs, iovsize);
	}

	__WINE_UNIX_API __wine_unix_rwv_status_t
	__wine_unix_pwritev_returns_status(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									   size_t iovsize, __wine_off_t offset) noexcept
	{
		return ::winelibc_nt::nt_pwritev(host_fd, iovs, iovsize, offset);
	}

	__WINE_UNIX_API __wine_unix_rwv_status_t
	__wine_unix_preadv_returns_status(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									  size_t iovsize, __wine_off_t offset) noexcept
	{
		return ::winelibc_nt::nt_preadv(host_fd, iovs, iovsize, offset);
	}

	__WINE_UNIX_API __wine_unix_rw_status_t
	__wine_unix_write_returns_status(__wine_host_fd_t host_fd, void const *buf, size_t len) noexcept
	{
		return ::winelibc_nt::nt_write(host_fd, buf, len);
	}

	__WINE_UNIX_API __wine_unix_rw_status_t
	__wine_unix_read_returns_status(__wine_host_fd_t host_fd, void *buf, size_t len) noexcept
	{
		return ::winelibc_nt::nt_read(host_fd, buf, len);
	}

	__WINE_UNIX_API __WINE_UNIX_CONST __wine_unix_host_fd_status_t
	__wine_unix_get_std_host_fd_returns_status(int which) noexcept
	{
		return ::winelibc_nt::nt_get_std_host_fd(which);
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
}

#endif // !defined(__WINE_UNIX_NT_INTERNAL__)
