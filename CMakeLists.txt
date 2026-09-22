cmake_minimum_required(VERSION 3.20)
project(winelibc CXX)

# ---------------------------------------------------------------------------
# winelibc builds two companion modules for the same PE-side ABI:
#
#   fast_io_wine.so   host unixlib, loaded under wine through
#                     NtQueryVirtualMemory(MemoryWineLoadUnixLibByName)
#   winelibc_nt.dll   NT-emulation dll exporting the same
#                     __wine_unix_call_funcs table over ntdll calls.
#
# The PE binary is identical either way; swapping the companion module is all
# it takes to run on wine vs. real windows.
# ---------------------------------------------------------------------------

option(WINELIBC_UNIXLIB "build the host unixlib (fast_io_wine.so)" ON)
option(WINELIBC_NT_EMULATION "build the NT-emulation PE dll + PE test exe" OFF)
option(WINELIBC_IMPORT_LIB "generate libwinelibc_nt.a with llvm-dlltool" ON)

# The PE side is compiled by a separate windows-targeting driver, not the
# project compiler. Point these at whatever clang cfg you use.
set(WINELIBC_PE_CXX "clang++" CACHE FILEPATH "PE-targeting C++ compiler driver")
set(WINELIBC_PE_CXXFLAGS "--config=$ENV{HOME}/herbcfgs/x86_64-windows-msvc.cfg"
	CACHE STRING "flags for the PE compiler (cmake list, -D;-I;--config=...)")
set(WINELIBC_PE_LIBS "-lntdll" CACHE STRING "libraries for PE targets")
set(WINELIBC_DLLTOOL "llvm-dlltool" CACHE FILEPATH "llvm-dlltool for import-lib generation")
set(WINELIBC_WINE "wine" CACHE FILEPATH "wine binary for the check target")

if(WINELIBC_UNIXLIB)
	if(WIN32)
		message(FATAL_ERROR "WINELIBC_UNIXLIB requires a unix host compiler")
	endif()
	add_library(fast_io_wine SHARED src/unixhost.cc)
	set_target_properties(fast_io_wine PROPERTIES
		PREFIX "" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
		POSITION_INDEPENDENT_CODE ON)
	target_include_directories(fast_io_wine PRIVATE include)
	target_compile_definitions(fast_io_wine PRIVATE WINE_UNIX_LIB=1)
endif()

if(WINELIBC_NT_EMULATION OR WINELIBC_IMPORT_LIB)
	separate_arguments(WINELIBC_PE_CXXFLAGS_LIST UNIX_COMMAND "${WINELIBC_PE_CXXFLAGS}")
	separate_arguments(WINELIBC_PE_LIBS_LIST UNIX_COMMAND "${WINELIBC_PE_LIBS}")
	set(WINELIBC_ABI_HEADERS
		include/__wine_unix/__wine_unix.h
		include/__wine_unix/__wine_unix_errno.h
		include/__wine_unix/__wine_unix_fcntl.h
		include/__wine_unix/__wine_unix_pe.h)
endif()

if(WINELIBC_NT_EMULATION)
	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/winelibc_nt.dll
		COMMAND ${WINELIBC_PE_CXX} ${WINELIBC_PE_CXXFLAGS_LIST} -shared
			-o ${CMAKE_BINARY_DIR}/winelibc_nt.dll
			${CMAKE_CURRENT_SOURCE_DIR}/src/ntemu.cc
			-I${CMAKE_CURRENT_SOURCE_DIR}/include ${WINELIBC_PE_LIBS_LIST}
		DEPENDS src/ntemu.cc ${WINELIBC_ABI_HEADERS}
		COMMENT "PE dll: winelibc_nt.dll (NT-emulation unixcall table)")

	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/winetest.exe
		COMMAND ${WINELIBC_PE_CXX} ${WINELIBC_PE_CXXFLAGS_LIST} -fherbceptions
			-o ${CMAKE_BINARY_DIR}/winetest.exe
			${CMAKE_CURRENT_SOURCE_DIR}/test/winetest.cc
			-I${CMAKE_CURRENT_SOURCE_DIR}/include ${WINELIBC_PE_LIBS_LIST} -lkernel32
		DEPENDS test/winetest.cc ${WINELIBC_ABI_HEADERS}
		COMMENT "PE exe: winetest.exe")

	add_custom_target(winelibc_nt ALL DEPENDS ${CMAKE_BINARY_DIR}/winelibc_nt.dll)
	add_custom_target(winetest ALL DEPENDS ${CMAKE_BINARY_DIR}/winetest.exe)
endif()

if(WINELIBC_IMPORT_LIB)
	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/libwinelibc_nt.a
		COMMAND ${WINELIBC_DLLTOOL}
			-d ${CMAKE_CURRENT_SOURCE_DIR}/def/winelibc_nt.def
			-D winelibc_nt.dll -l ${CMAKE_BINARY_DIR}/libwinelibc_nt.a
		DEPENDS def/winelibc_nt.def
		COMMENT "import lib: libwinelibc_nt.a")
	add_custom_target(winelibc_nt_import ALL DEPENDS ${CMAKE_BINARY_DIR}/libwinelibc_nt.a)
endif()

# wine ./winetest.exe       -> unixcall path (fast_io_wine.so via WINEDLLPATH)
# wine ./winetest.exe nt    -> winelibc_nt.dll path
if(WINELIBC_UNIXLIB AND WINELIBC_NT_EMULATION)
	add_custom_target(check
		COMMAND ${CMAKE_COMMAND} -E env
			WINEDLLPATH=${CMAKE_BINARY_DIR}
			${WINELIBC_WINE} ${CMAKE_BINARY_DIR}/winetest.exe
		COMMAND ${CMAKE_COMMAND} -E env
			WINEDLLPATH=${CMAKE_BINARY_DIR}
			${WINELIBC_WINE} ${CMAKE_BINARY_DIR}/winetest.exe nt
		DEPENDS fast_io_wine winelibc_nt winetest
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "running winetest under wine (unixcall + nt-emu)")
endif()
