/*
winelibc_nt.dll — the NT side of the __wine_unix contract.

Compiled as a PE dll (clang --target=*-windows-msvc, no wine toolchain). It
exports __wine_unix_call_funcs[] exactly like the host unixlib does, but every
entry forwards to ntdll instead of the host libc. A PE binary that talks the
__wine_unix ABI therefore works unchanged on real windows: on wine the table
comes from fast_io_wine.so through __wine_unix_call_dispatcher; here it is
imported straight from this dll.

	clang++ --config=$HOME/herbcfgs/x86_64-windows-msvc.cfg -shared \
		-o winelibc_nt.dll src/ntemu.cc -Iinclude -lntdll
*/

#include <__wine_unix/__wine_unix.h>
#include <__wine_unix/__wine_unix_errno.h>
#include <__wine_unix/__wine_unix_fcntl.h>

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

template <typename T>
inline ::std::uintptr_t param_addr(T v) noexcept
{
	if constexpr (::std::is_pointer_v<T>)
	{
		return reinterpret_cast<::std::uintptr_t>(v);
	}
	else
	{
		return static_cast<::std::uintptr_t>(v);
	}
}

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

__wine_unix_status_t nt_host_fd_to_unix_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_host_fd_to_unix_fd_params_t *>(args)};
	void *handle{};
	if (auto const err{host_fd_to_handle(params->host_fd, handle)}; err)
	{
		params->unix_fd = -1;
		return err;
	}
	auto const v{reinterpret_cast<::std::uintptr_t>(handle)};
	if (static_cast<::std::uintptr_t>(static_cast<int>(v)) != v)
	{
		params->unix_fd = -1;
		return __WINE_UNIX_ERRNO_EBADF;
	}
	params->unix_fd = static_cast<int>(v);
	return __WINE_UNIX_ERRNO_SUCCESS;
}

__wine_unix_status_t nt_unix_fd_to_host_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_unix_fd_to_host_fd_params_t *>(args)};
	params->host_fd = params->unix_fd < 0 ? 0 : static_cast<__wine_host_fd_t>(params->unix_fd);
	return __WINE_UNIX_ERRNO_SUCCESS;
}

__wine_unix_status_t nt_host_fd_to_nt_handle(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_host_fd_to_nt_handle_params_t *>(args)};
	void *handle{};
	if (auto const err{host_fd_to_handle(params->host_fd, handle)}; err)
	{
		return err;
	}
	params->handle = static_cast<ptrdiff_t>(reinterpret_cast<::std::uintptr_t>(handle));
	return __WINE_UNIX_ERRNO_SUCCESS;
}

__wine_unix_status_t nt_nt_handle_to_host_fd(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_nt_handle_to_host_fd_params_t *>(args)};
	params->host_fd = handle_to_host_fd(reinterpret_cast<void *>(static_cast<::std::uintptr_t>(params->handle)));
	return __WINE_UNIX_ERRNO_SUCCESS;
}

__wine_unix_status_t nt_openat(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_openat_params_t *>(args)};
	auto const *filename{reinterpret_cast<char const *>(param_addr(params->filename))};
	auto const filenamelen{static_cast<::std::size_t>(params->filenamelen)};

	char16_t ntbfr[nt_path_max];
	unicode_string us{0, static_cast<uint16_t>(sizeof(ntbfr)), ntbfr};
	void *rootdir{};
	::std::size_t wlen{};
	if (params->host_dirfd == 0)
	{
		wlen = unix_abs_to_nt(filename, filenamelen, ntbfr, nt_path_max - 1);
	}
	else
	{
		if (auto const err{host_fd_to_handle(static_cast<__wine_host_fd_t>(params->host_dirfd), rootdir)}; err)
		{
			return err;
		}
		wlen = unix_rel_to_nt(filename, filenamelen, ntbfr, nt_path_max - 1);
	}
	if (wlen == 0)
	{
		return __WINE_UNIX_ERRNO_ENAMETOOLONG;
	}
	us.Length = static_cast<uint16_t>(wlen * sizeof(char16_t));

	auto const flags{static_cast<__wine_host_flags_t>(params->flags)};
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
	params->host_fd = static_cast<decltype(params->host_fd)>(handle_to_host_fd(handle));
	return ntstatus_to_wine_errno(status);
}

__wine_unix_status_t nt_close(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_close_params_t *>(args)};
	void *handle{};
	if (auto const err{host_fd_to_handle(static_cast<__wine_host_fd_t>(params->host_fd), handle)}; err)
	{
		return err;
	}
	return ntstatus_to_wine_errno(NtClose(handle));
}

template <typename Params, bool Write>
__wine_unix_status_t readwritev_common(Params *params, int64_t const *byte_offset) noexcept
{
	void *handle{};
	if (auto const err{host_fd_to_handle(static_cast<__wine_host_fd_t>(params->host_fd), handle)}; err)
	{
		return err;
	}
	auto const *iovs{reinterpret_cast<__wine_unix_iovec_t const *>(param_addr(params->iovs))};
	auto const iovsize{static_cast<::std::size_t>(params->iovsize)};
	::std::size_t total{};
	::std::size_t baseindex{};
	::std::size_t index{};
	__wine_unix_status_t ret{__WINE_UNIX_ERRNO_SUCCESS};
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
			off = *byte_offset + static_cast<int64_t>(total);
			offp = &off;
		}
		auto const status{Write ? NtWriteFile(handle, nullptr, nullptr, nullptr, &iosb, base,
											static_cast<uint32_t>(ilen), offp, nullptr)
								: NtReadFile(handle, nullptr, nullptr, nullptr, &iosb, base,
											 static_cast<uint32_t>(ilen), offp, nullptr)};
		if (status)
		{
			ret = ntstatus_to_wine_errno(status);
			break;
		}
		auto const done{static_cast<::std::size_t>(iosb.Information)};
		total += done;
		if (done < ilen)
		{
			baseindex = i;
			index = done;
			goto done;
		}
	}
	if (ret == __WINE_UNIX_ERRNO_SUCCESS)
	{
		baseindex = iovsize;
		index = 0;
	}
done:
	params->total = static_cast<decltype(params->total)>(total);
	params->baseindex = static_cast<decltype(params->baseindex)>(baseindex);
	params->index = static_cast<decltype(params->index)>(index);
	return ret;
}

__wine_unix_status_t nt_writev(void *args) noexcept
{
	return readwritev_common<__wine_unix_readwritev_params_t, true>(
		static_cast<__wine_unix_readwritev_params_t *>(args), nullptr);
}

__wine_unix_status_t nt_readv(void *args) noexcept
{
	return readwritev_common<__wine_unix_readwritev_params_t, false>(
		static_cast<__wine_unix_readwritev_params_t *>(args), nullptr);
}

__wine_unix_status_t nt_pwritev(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_preadwritev_params_t *>(args)};
	auto const offset{static_cast<int64_t>(params->offset)};
	return readwritev_common<__wine_unix_preadwritev_params_t, true>(params, &offset);
}

__wine_unix_status_t nt_preadv(void *args) noexcept
{
	auto *params{static_cast<__wine_unix_preadwritev_params_t *>(args)};
	auto const offset{static_cast<int64_t>(params->offset)};
	return readwritev_common<__wine_unix_preadwritev_params_t, false>(params, &offset);
}

} // namespace
} // namespace winelibc_nt

extern "C"
{
	extern __WINE_UNIX_DLLEXPORT __wine_unixlib_entry_t const __wine_unix_call_funcs[] = {
		::winelibc_nt::nt_host_fd_to_unix_fd, ::winelibc_nt::nt_unix_fd_to_host_fd,
		::winelibc_nt::nt_host_fd_to_nt_handle, ::winelibc_nt::nt_nt_handle_to_host_fd,
		::winelibc_nt::nt_openat, ::winelibc_nt::nt_close, ::winelibc_nt::nt_writev,
		::winelibc_nt::nt_readv, ::winelibc_nt::nt_pwritev, ::winelibc_nt::nt_preadv,
	};

	__WINE_UNIX_DLLEXPORT __wine_unix_status_t __wine_unix_lib_init(void) noexcept
	{
		return __WINE_UNIX_ERRNO_SUCCESS;
	}

	__declspec(dllexport) int __stdcall DllMain(void *, uint32_t, void *) noexcept
	{
		return 1;
	}
}

static_assert(sizeof(__wine_unix_call_funcs) / sizeof(__wine_unixlib_entry_t) == __wine_unix_funcs_count,
			  "__wine_unix_call_funcs must match the __wine_unix_funcs enum");
