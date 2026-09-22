#pragma once
/*
Private: minimal ntdll import surface shared by the wineunix.dll
implementation TUs (src/wineunix.cc, src/wineunix_nt.cc, and
src/wineunix_bundled.cc which #includes both into one TU).

Every import is declared once here under an ntdll_* identifier bound to the
real symbol by an __asm__ label — the C++ names never collide with anything,
whichever sources share the TU.
*/

#include <cstddef>
#include <cstdint>

/* stdcall symbol renaming — same 4 forms as fast_io's FAST_IO_WINSTDCALL_RENAME */
#if defined(_M_HYBRID)
#define __WINE_UNIX_NT_RENAME(name, count) __asm__("#" #name "@" #count)
#elif defined(__arm64ec__)
#define __WINE_UNIX_NT_RENAME(name, count) __asm__("#" #name)
#elif SIZE_MAX <= UINT_LEAST32_MAX && (defined(__x86__) || defined(_M_IX86) || defined(__i386__))
#if !defined(__clang__)
#define __WINE_UNIX_NT_RENAME(name, count) __asm__(#name "@" #count)
#else
#define __WINE_UNIX_NT_RENAME(name, count) __asm__("_" #name "@" #count)
#endif
#else
#define __WINE_UNIX_NT_RENAME(name, count) __asm__(#name)
#endif

struct unicode_string /* UNICODE_STRING */
{
	uint16_t Length;
	uint16_t MaximumLength;
	char16_t *Buffer;
};

struct ansi_string /* ANSI_STRING */
{
	uint16_t Length;
	uint16_t MaximumLength;
	char *Buffer;
};

struct object_attributes /* OBJECT_ATTRIBUTES */
{
	uint32_t Length;
	void *RootDirectory;
	unicode_string *ObjectName;
	uint32_t Attributes;
	void *SecurityDescriptor;
	void *SecurityQualityOfService;
};

struct io_status_block /* IO_STATUS_BLOCK */
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
	__declspec(dllimport) int32_t __stdcall ntdll_NtClose(void *handle) noexcept
		__WINE_UNIX_NT_RENAME(NtClose, 4);
	__declspec(dllimport) int32_t __stdcall ntdll_NtCreateFile(void **handle, uint32_t desired_access,
															 object_attributes *objattr,
															 io_status_block *iosb, int64_t *alloc_size,
															 uint32_t file_attributes, uint32_t share_access,
															 uint32_t create_disposition,
															 uint32_t create_options, void *ea_buffer,
															 uint32_t ea_length) noexcept
		__WINE_UNIX_NT_RENAME(NtCreateFile, 44);
	__declspec(dllimport) int32_t __stdcall ntdll_NtReadFile(void *handle, void *event, void *apc_routine,
														   void *apc_context, io_status_block *iosb,
														   void *buffer, uint32_t length,
														   int64_t *byte_offset, uint32_t *key) noexcept
		__WINE_UNIX_NT_RENAME(NtReadFile, 36);
	__declspec(dllimport) int32_t __stdcall ntdll_NtWriteFile(void *handle, void *event, void *apc_routine,
															void *apc_context, io_status_block *iosb,
															void const *buffer, uint32_t length,
															int64_t *byte_offset, uint32_t *key) noexcept
		__WINE_UNIX_NT_RENAME(NtWriteFile, 36);
	__declspec(dllimport) int32_t __stdcall ntdll_NtQueryVirtualMemory(void *process, void const *addr,
																	 uint32_t info_class, void *buffer,
																	 size_t len, size_t *res_len) noexcept
		__WINE_UNIX_NT_RENAME(NtQueryVirtualMemory, 24);
	__declspec(dllimport) int32_t __stdcall ntdll_LdrGetDllHandle(char16_t const *path,
																uint32_t *characteristics,
																unicode_string *name,
																void **handle) noexcept
		__WINE_UNIX_NT_RENAME(LdrGetDllHandle, 16);
	__declspec(dllimport) int32_t __stdcall ntdll_LdrGetProcedureAddress(void *handle,
																	   ansi_string const *name,
																	   uint32_t ordinal,
																	   void **proc) noexcept
		__WINE_UNIX_NT_RENAME(LdrGetProcedureAddress, 16);
	__declspec(dllimport) void *__stdcall ntdll_RtlAllocateHeap(void *heap, uint32_t flags,
															  uintptr_t size) noexcept
		__WINE_UNIX_NT_RENAME(RtlAllocateHeap, 12);
	__declspec(dllimport) int __stdcall ntdll_RtlFreeHeap(void *heap, uint32_t flags, void *ptr) noexcept
		__WINE_UNIX_NT_RENAME(RtlFreeHeap, 12);
	__declspec(dllimport) void *__stdcall ntdll_RtlGetCurrentPeb() noexcept
		__WINE_UNIX_NT_RENAME(RtlGetCurrentPeb, 0);
	__declspec(dllimport) void __stdcall ntdll_RtlInitUnicodeString(unicode_string *dst,
																  char16_t const *src) noexcept
		__WINE_UNIX_NT_RENAME(RtlInitUnicodeString, 8);
}
