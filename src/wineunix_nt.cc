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

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace winelibc_nt
{
namespace
{

/* ---- minimal NT declarations (ntdll.lib) ------------------------------- */

struct unicode_string
{
	uint16_t Length;
	uint16_t MaximumLength;
	char16_t *Buffer;
};

struct ansi_string
{
	uint16_t Length;
	uint16_t MaximumLength;
	char *Buffer;
};

struct object_attributes
{
	uint32_t Length;
	void *RootDirectory;
	unicode_string *ObjectName;
	uint32_t Attributes;
	void *SecurityDescriptor;
	void *SecurityQualityOfService;
};

struct io_status_block
{
	union
	{
		int32_t Status;
		void *Pointer;
	};
	uintptr_t Information;
};

extern "C"
{
	__declspec(dllimport) int32_t __stdcall NtCreateFile(void **handle, uint32_t desired_access,
													   object_attributes *objattr, io_status_block *iosb,
													   int64_t *alloc_size, uint32_t file_attributes,
													   uint32_t share_access, uint32_t create_disposition,
													   uint32_t create_options, void *ea_buffer,
													   uint32_t ea_length) noexcept;
	__declspec(dllimport) int32_t __stdcall NtReadFile(void *handle, void *event, void *apc_routine,
													 void *apc_context, io_status_block *iosb, void *buffer,
													 uint32_t length, int64_t *byte_offset,
													 uint32_t *key) noexcept;
	__declspec(dllimport) int32_t __stdcall NtWriteFile(void *handle, void *event, void *apc_routine,
													  void *apc_context, io_status_block *iosb, void const *buffer,
													  uint32_t length, int64_t *byte_offset,
													  uint32_t *key) noexcept;
	__declspec(dllimport) int32_t __stdcall NtClose(void *handle) noexcept;
	__declspec(dllimport) void __stdcall RtlInitUnicodeString(unicode_string *dst,
															char16_t const *src) noexcept;
	__declspec(dllimport) int32_t __stdcall LdrGetDllHandle(char16_t const *path, uint32_t *characteristics,
														  unicode_string *name, void **handle) noexcept;
	__declspec(dllimport) int32_t __stdcall LdrGetProcedureAddress(void *handle, ansi_string const *name,
																 uint32_t ordinal, void **proc) noexcept;
}

constexpr uint32_t nt_status_success{0};
constexpr uint32_t obj_case_insensitive{0x40};

constexpr uint32_t file_read_data{0x0001};
constexpr uint32_t file_write_data{0x0002};
constexpr uint32_t file_append_data{0x0004};
constexpr uint32_t file_read_attributes{0x0080};
constexpr uint32_t file_write_attributes{0x0100};
constexpr uint32_t delete_access{0x00010000};
constexpr uint32_t synchronize{0x00100000};

constexpr uint32_t file_share_read{0x00000001};
constexpr uint32_t file_share_write{0x00000002};
constexpr uint32_t file_share_delete{0x00000004};

constexpr uint32_t file_open{0x00000001};
constexpr uint32_t file_create{0x00000002};
constexpr uint32_t file_open_if{0x00000003};
constexpr uint32_t file_overwrite{0x00000004};
constexpr uint32_t file_overwrite_if{0x00000005};

constexpr uint32_t file_directory_file{0x00000001};
constexpr uint32_t file_write_through{0x00000002};
constexpr uint32_t file_no_intermediate_buffering{0x00000008};
constexpr uint32_t file_synchronous_io_nonalert{0x00000020};
constexpr uint32_t file_non_directory_file{0x00000040};
constexpr uint32_t file_delete_on_close{0x00001000};
constexpr uint32_t file_open_reparse_point{0x00200000};
constexpr uint32_t file_open_for_backup_intent{0x00004000};

constexpr uint32_t file_attribute_normal{0x00000080};

constexpr uint32_t status_object_name_not_found{0xC0000034};
constexpr uint32_t status_object_path_not_found{0xC000003A};
constexpr uint32_t status_object_name_collision{0xC0000035};
constexpr uint32_t status_access_denied{0xC0000022};
constexpr uint32_t status_sharing_violation{0xC0000043};
constexpr uint32_t status_invalid_parameter{0xC000000D};
constexpr uint32_t status_invalid_handle{0xC0000008};
constexpr uint32_t status_not_a_directory{0xC0000103};
constexpr uint32_t status_file_is_a_directory{0xC00000BA};
constexpr uint32_t status_name_too_long{0xC0000106};
constexpr uint32_t status_disk_full{0xC000007F};
constexpr uint32_t status_insufficient_resources{0xC000009A};

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
		if (LdrGetDllHandle(nullptr, nullptr, &us, &ntdll))
		{
			return u'C';
		}
		char probe[] = "wine_get_version";
		ansi_string as{static_cast<uint16_t>(sizeof(probe) - 1), static_cast<uint16_t>(sizeof(probe)), probe};
		void *proc{};
		if (LdrGetProcedureAddress(ntdll, &as, 0, &proc) || proc == nullptr)
		{
			return u'C';
		}
		return u'Z';
	}()};
	return drive;
}

constexpr ::std::size_t nt_path_max{4096};

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

__wine_unix_nt_handle_status_t nt_host_fd_to_nt_handle(__wine_host_fd_t host_fd) noexcept
{
	void *handle{};
	if (auto const err{host_fd_to_handle(host_fd, handle)}; err)
	{
		return {err, 0};
	}
	return {__WINE_UNIX_ERRNO_SUCCESS,
			static_cast<ptrdiff_t>(reinterpret_cast<::std::uintptr_t>(handle))};
}

__wine_unix_host_fd_status_t nt_nt_handle_to_host_fd(ptrdiff_t handle) noexcept
{
	return {__WINE_UNIX_ERRNO_SUCCESS,
			handle_to_host_fd(reinterpret_cast<void *>(static_cast<::std::uintptr_t>(handle)))};
}

__wine_unix_host_fd_status_t nt_openat(__wine_host_fd_t host_dirfd, char const *filename,
									   ::std::size_t filenamelen, __wine_host_flags_t flags,
									   __wine_host_mode_t mode) noexcept
{
	char16_t ntbfr[nt_path_max];
	unicode_string us{0, static_cast<uint16_t>(sizeof(ntbfr)), ntbfr};
	void *rootdir{};
	::std::size_t wlen{};
	if (host_dirfd == 0)
	{
		wlen = unix_abs_to_nt(filename, filenamelen, ntbfr, nt_path_max - 1);
	}
	else
	{
		if (auto const err{host_fd_to_handle(host_dirfd, rootdir)}; err)
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
	auto const status{NtCreateFile(&handle, access, &oa, &iosb, nullptr, file_attribute_normal,
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
	return ntstatus_to_wine_errno(NtClose(handle));
}

template <bool Write>
__wine_unix_rwv_status_t readwritev_common(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
										   ::std::size_t iovsize, int64_t const *byte_offset) noexcept
{
	__wine_unix_rwv_status_t r{__WINE_UNIX_ERRNO_SUCCESS, 0, 0, 0};
	void *handle{};
	if (auto const err{host_fd_to_handle(host_fd, handle)}; err)
	{
		r.status = err;
		return r;
	}
	for (::std::size_t i{}; i != iovsize; ++i)
	{
		auto const ilen{static_cast<::std::size_t>(iovs[i].iov_len)};
		if (ilen == 0)
		{
			continue;
		}
		auto *const base{reinterpret_cast<void *>(const_cast<char *>(static_cast<char const *>(iovs[i].iov_base)))};
		io_status_block iosb{};
		int64_t off{};
		int64_t *offp{nullptr};
		if (byte_offset != nullptr)
		{
			off = *byte_offset + static_cast<int64_t>(r.total);
			offp = &off;
		}
		auto const status{Write ? NtWriteFile(handle, nullptr, nullptr, nullptr, &iosb, base,
											static_cast<uint32_t>(ilen), offp, nullptr)
								: NtReadFile(handle, nullptr, nullptr, nullptr, &iosb, base,
											 static_cast<uint32_t>(ilen), offp, nullptr)};
		if (status)
		{
			r.status = ntstatus_to_wine_errno(status);
			break;
		}
		auto const done{static_cast<::std::size_t>(iosb.Information)};
		r.total += done;
		if (done < ilen)
		{
			r.baseindex = i;
			r.index = done;
			return r;
		}
	}
	if (r.status == __WINE_UNIX_ERRNO_SUCCESS)
	{
		r.baseindex = iovsize;
		r.index = 0;
	}
	return r;
}

__wine_unix_rwv_status_t nt_writev(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
								   ::std::size_t iovsize) noexcept
{
	return readwritev_common<true>(host_fd, iovs, iovsize, nullptr);
}

__wine_unix_rwv_status_t nt_readv(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
								  ::std::size_t iovsize) noexcept
{
	return readwritev_common<false>(host_fd, iovs, iovsize, nullptr);
}

__wine_unix_rwv_status_t nt_pwritev(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
									::std::size_t iovsize, __wine_off_t offset) noexcept
{
	auto const off{static_cast<int64_t>(offset)};
	return readwritev_common<true>(host_fd, iovs, iovsize, &off);
}

__wine_unix_rwv_status_t nt_preadv(__wine_host_fd_t host_fd, __wine_unix_iovec_t const *iovs,
								   ::std::size_t iovsize, __wine_off_t offset) noexcept
{
	auto const off{static_cast<int64_t>(offset)};
	return readwritev_common<false>(host_fd, iovs, iovsize, &off);
}

} // namespace
} // namespace winelibc_nt

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

#endif

	__declspec(dllexport) int __stdcall DllMain(void *, uint32_t, void *) noexcept
	{
		return 1;
	}
}
