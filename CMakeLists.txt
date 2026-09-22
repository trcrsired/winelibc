cmake_minimum_required(VERSION 3.20)
project(winelibc CXX)

# ---------------------------------------------------------------------------
# winelibc ships one public api (include/__wine_unix/__wine_unix.h) exported by
# wineunix.dll, which exists in two implementations:
#
#   unixcall/wineunix.dll   packs each call into its params struct and runs it
#                           through ntdll's __wine_unix_call_dispatcher into
#                           libwineunix.so, which calls the host libc.
#                           user -> wineunix.dll -> libwineunix.so -> libc
#   nt/wineunix.dll         forwards straight to ntdll.
#                           user -> wineunix.dll -> ntdll
#
# A PE binary links libwineunix.a once; which implementation it runs against
# depends only on which wineunix.dll is shipped next to it.
# ---------------------------------------------------------------------------

option(WINELIBC_UNIXLIB "build the host unixlib (libwineunix.so)" ON)
option(WINELIBC_WINEUNIX_DLL "build the PE wineunix.dll variants + PE test exe" OFF)
option(WINELIBC_IMPORT_LIB "generate libwineunix.a with llvm-dlltool" ON)

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
	add_library(wineunix SHARED src/unixhost.cc)
	set_target_properties(wineunix PROPERTIES
		PREFIX "lib" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
		POSITION_INDEPENDENT_CODE ON)
	target_include_directories(wineunix PRIVATE include)
	target_compile_definitions(wineunix PRIVATE WINE_UNIX_LIB=1)
endif()

if(WINELIBC_WINEUNIX_DLL OR WINELIBC_IMPORT_LIB)
	separate_arguments(WINELIBC_PE_CXXFLAGS_LIST UNIX_COMMAND "${WINELIBC_PE_CXXFLAGS}")
	separate_arguments(WINELIBC_PE_LIBS_LIST UNIX_COMMAND "${WINELIBC_PE_LIBS}")
	set(WINELIBC_ABI_HEADERS
		include/__wine_unix/__wine_unix.h
		include/__wine_unix/__wine_unix_abi.h
		include/__wine_unix/__wine_unix_errno.h
		include/__wine_unix/__wine_unix_fcntl.h)
	file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/unixcall ${CMAKE_BINARY_DIR}/nt)
endif()

if(WINELIBC_WINEUNIX_DLL)
	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/unixcall/wineunix.dll
		COMMAND ${WINELIBC_PE_CXX} ${WINELIBC_PE_CXXFLAGS_LIST} -fherbceptions -shared
			-o ${CMAKE_BINARY_DIR}/unixcall/wineunix.dll
			${CMAKE_CURRENT_SOURCE_DIR}/src/wineunix.cc
			-I${CMAKE_CURRENT_SOURCE_DIR}/include ${WINELIBC_PE_LIBS_LIST}
		DEPENDS src/wineunix.cc ${WINELIBC_ABI_HEADERS}
		COMMENT "PE dll: unixcall/wineunix.dll (unixcall -> libwineunix.so)")

	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/nt/wineunix.dll
		COMMAND ${WINELIBC_PE_CXX} ${WINELIBC_PE_CXXFLAGS_LIST} -fherbceptions -shared
			-o ${CMAKE_BINARY_DIR}/nt/wineunix.dll
			${CMAKE_CURRENT_SOURCE_DIR}/src/wineunix_nt.cc
			-I${CMAKE_CURRENT_SOURCE_DIR}/include ${WINELIBC_PE_LIBS_LIST}
		DEPENDS src/wineunix_nt.cc ${WINELIBC_ABI_HEADERS}
		COMMENT "PE dll: nt/wineunix.dll (ntdll forwarding)")

	# identical test exe next to each dll; the sibling wineunix.dll wins
	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/unixcall/winetest.exe
		COMMAND ${WINELIBC_PE_CXX} ${WINELIBC_PE_CXXFLAGS_LIST} -fherbceptions
			-o ${CMAKE_BINARY_DIR}/unixcall/winetest.exe
			${CMAKE_CURRENT_SOURCE_DIR}/test/winetest.cc
			-I${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/libwineunix.a
		DEPENDS test/winetest.cc ${CMAKE_BINARY_DIR}/libwineunix.a ${WINELIBC_ABI_HEADERS}
		COMMENT "PE exe: unixcall/winetest.exe")

	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/nt/winetest.exe
		COMMAND ${WINELIBC_PE_CXX} ${WINELIBC_PE_CXXFLAGS_LIST} -fherbceptions
			-o ${CMAKE_BINARY_DIR}/nt/winetest.exe
			${CMAKE_CURRENT_SOURCE_DIR}/test/winetest.cc
			-I${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/libwineunix.a
		DEPENDS test/winetest.cc ${CMAKE_BINARY_DIR}/libwineunix.a ${WINELIBC_ABI_HEADERS}
		COMMENT "PE exe: nt/winetest.exe")

	add_custom_target(wineunix_dlls ALL
		DEPENDS ${CMAKE_BINARY_DIR}/unixcall/wineunix.dll ${CMAKE_BINARY_DIR}/nt/wineunix.dll)
	add_custom_target(winetest ALL
		DEPENDS ${CMAKE_BINARY_DIR}/unixcall/winetest.exe ${CMAKE_BINARY_DIR}/nt/winetest.exe)
endif()

if(WINELIBC_IMPORT_LIB)
	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/libwineunix.a
		COMMAND ${WINELIBC_DLLTOOL}
			-d ${CMAKE_CURRENT_SOURCE_DIR}/def/wineunix.def
			-D wineunix.dll -l ${CMAKE_BINARY_DIR}/libwineunix.a
		DEPENDS def/wineunix.def
		COMMENT "import lib: libwineunix.a")
	add_custom_target(wineunix_import ALL DEPENDS ${CMAKE_BINARY_DIR}/libwineunix.a)
endif()

# wine unixcall/winetest.exe -> unixcall dll -> libwineunix.so (via WINEDLLPATH)
# wine nt/winetest.exe       -> nt dll -> ntdll
if(WINELIBC_UNIXLIB AND WINELIBC_WINEUNIX_DLL AND WINELIBC_IMPORT_LIB)
	add_custom_target(check
		COMMAND ${CMAKE_COMMAND} -E env
			WINEDLLPATH=${CMAKE_BINARY_DIR}
			${WINELIBC_WINE} ${CMAKE_BINARY_DIR}/unixcall/winetest.exe
		COMMAND ${CMAKE_COMMAND} -E env
			WINEDLLPATH=${CMAKE_BINARY_DIR}
			${WINELIBC_WINE} ${CMAKE_BINARY_DIR}/nt/winetest.exe
		DEPENDS wineunix wineunix_dlls winetest
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "running winetest under wine (unixcall + nt)")
endif()
