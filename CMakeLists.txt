cmake_minimum_required(VERSION 3.20)
project(libwineunix CXX)

# ---------------------------------------------------------------------------
# libwineunix ships one public api (include/__wine_unix/__wine_unix.h) exported by
# wineunix.dll. LIBWINEUNIX_WINEUNIX_IMPL picks the implementation:
#
#   bundled   (default) one dll carrying both paths: calls run through
#             libwineunix.so when it resolves (under wine), and straight to
#             ntdll otherwise (real windows).
#               user -> wineunix.dll -> libwineunix.so -> libc   (wine)
#               user -> wineunix.dll -> ntdll                    (windows)
#   unixcall  unixcall path only — needs libwineunix.so (wine only)
#   nt        ntdll forwarding only
#
# A PE binary links libwineunix.a once; the bundled wineunix.dll then works
# under wine and on real windows with no rebuild and no dll swapping.
# ---------------------------------------------------------------------------

option(LIBWINEUNIX_UNIXLIB "build the host unixlib (libwineunix.so)" ON)
option(LIBWINEUNIX_WINEUNIX_DLL "build the PE wineunix.dll" OFF)
set(LIBWINEUNIX_WINEUNIX_IMPL "bundled" CACHE STRING
	"wineunix.dll implementation: bundled, unixcall or nt")
set_property(CACHE LIBWINEUNIX_WINEUNIX_IMPL PROPERTY STRINGS bundled unixcall nt)
option(LIBWINEUNIX_IMPORT_LIB "generate libwineunix.a with llvm-dlltool" ON)
option(LIBWINEUNIX_TEST "build the PE test exes (needs LIBWINEUNIX_WINEUNIX_DLL + LIBWINEUNIX_IMPORT_LIB)" OFF)
option(LIBWINEUNIX_FAST_IO_TEST "build the fast_io wine layer test (needs fast_io headers)" OFF)

# The PE side is compiled by a separate windows-targeting driver, not the
# project compiler. Point these at whatever clang cfg you use.
set(LIBWINEUNIX_PE_CXX "clang++" CACHE FILEPATH "PE-targeting C++ compiler driver")
set(LIBWINEUNIX_PE_CXXFLAGS "" CACHE STRING
	"flags for the PE compiler (cmake list, -D;-I;--config=...)")
set(LIBWINEUNIX_PE_LIBS "-lntdll" CACHE STRING "libraries for PE targets")
set(LIBWINEUNIX_DLLTOOL "llvm-dlltool" CACHE FILEPATH "llvm-dlltool for import-lib generation")
set(LIBWINEUNIX_PE_MACHINE "" CACHE STRING
	"llvm-dlltool -m machine for the PE import lib (i386, i386:x86-64, arm, arm64, arm64ec); empty derives it from CMAKE_SYSTEM_PROCESSOR")
set(LIBWINEUNIX_WINE "wine" CACHE FILEPATH "wine binary for the check target")
set(LIBWINEUNIX_FAST_IO_INCLUDE "" CACHE PATH
	"fast_io include dir for the fast_io wine layer test")

if(NOT LIBWINEUNIX_WINEUNIX_IMPL MATCHES "^(bundled|unixcall|nt)$")
	message(FATAL_ERROR "LIBWINEUNIX_WINEUNIX_IMPL must be bundled, unixcall or nt "
		"(got '${LIBWINEUNIX_WINEUNIX_IMPL}')")
endif()

if(LIBWINEUNIX_FAST_IO_TEST AND (NOT LIBWINEUNIX_FAST_IO_INCLUDE OR NOT EXISTS "${LIBWINEUNIX_FAST_IO_INCLUDE}/fast_io.h"))
	message(WARNING "LIBWINEUNIX_FAST_IO_TEST is on but fast_io.h was not found under "
		"'${LIBWINEUNIX_FAST_IO_INCLUDE}'; disabling (set LIBWINEUNIX_FAST_IO_INCLUDE)")
	set(LIBWINEUNIX_FAST_IO_TEST OFF)
endif()

if((LIBWINEUNIX_TEST OR LIBWINEUNIX_FAST_IO_TEST) AND NOT (LIBWINEUNIX_WINEUNIX_DLL AND LIBWINEUNIX_IMPORT_LIB))
	message(WARNING "test exes link libwineunix.a next to a wineunix.dll; "
		"enabling LIBWINEUNIX_WINEUNIX_DLL and LIBWINEUNIX_IMPORT_LIB")
	set(LIBWINEUNIX_WINEUNIX_DLL ON)
	set(LIBWINEUNIX_IMPORT_LIB ON)
endif()

if(LIBWINEUNIX_UNIXLIB)
	if(WIN32)
		message(FATAL_ERROR "LIBWINEUNIX_UNIXLIB requires a unix host compiler")
	endif()
	add_library(wineunix SHARED src/unixhost.cc src/unixhost_errno.cc)
	set_target_properties(wineunix PROPERTIES
		PREFIX "lib" CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
		POSITION_INDEPENDENT_CODE ON)
	target_include_directories(wineunix PRIVATE include)
	target_compile_definitions(wineunix PRIVATE WINE_UNIX_LIB=1)
endif()

if(LIBWINEUNIX_WINEUNIX_DLL OR LIBWINEUNIX_IMPORT_LIB)
	separate_arguments(LIBWINEUNIX_PE_CXXFLAGS_LIST UNIX_COMMAND "${LIBWINEUNIX_PE_CXXFLAGS}")
	separate_arguments(LIBWINEUNIX_PE_LIBS_LIST UNIX_COMMAND "${LIBWINEUNIX_PE_LIBS}")
	set(LIBWINEUNIX_ABI_HEADERS
		include/__wine_unix/__wine_unix.h
		include/__wine_unix/__wine_unix_errno.h
		include/__wine_unix/__wine_unix_fcntl.h
		src/__wine_unix_abi.h)
endif()

if(LIBWINEUNIX_WINEUNIX_DLL)
	if(LIBWINEUNIX_WINEUNIX_IMPL STREQUAL "bundled")
		set(LIBWINEUNIX_WINEUNIX_SRC src/wineunix_bundled.cc)
		set(LIBWINEUNIX_WINEUNIX_SRC_DEPS src/wineunix.cc src/wineunix_nt.cc)
	elseif(LIBWINEUNIX_WINEUNIX_IMPL STREQUAL "unixcall")
		set(LIBWINEUNIX_WINEUNIX_SRC src/wineunix.cc)
	elseif(LIBWINEUNIX_WINEUNIX_IMPL STREQUAL "nt")
		set(LIBWINEUNIX_WINEUNIX_SRC src/wineunix_nt.cc)
	endif()

	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/wineunix.dll
		COMMAND ${LIBWINEUNIX_PE_CXX} ${LIBWINEUNIX_PE_CXXFLAGS_LIST} -fherbceptions -shared
			-o ${CMAKE_BINARY_DIR}/wineunix.dll
			${CMAKE_CURRENT_SOURCE_DIR}/${LIBWINEUNIX_WINEUNIX_SRC}
			-I${CMAKE_CURRENT_SOURCE_DIR}/include ${LIBWINEUNIX_PE_LIBS_LIST}
		DEPENDS ${LIBWINEUNIX_WINEUNIX_SRC} ${LIBWINEUNIX_WINEUNIX_SRC_DEPS} ${LIBWINEUNIX_ABI_HEADERS}
		COMMENT "PE dll: wineunix.dll (${LIBWINEUNIX_WINEUNIX_IMPL})")

	add_custom_target(wineunix_dll ALL DEPENDS ${CMAKE_BINARY_DIR}/wineunix.dll)

	foreach(testexe winetest fast_io_winetest)
		if((testexe STREQUAL "winetest" AND LIBWINEUNIX_TEST)
				OR (testexe STREQUAL "fast_io_winetest" AND LIBWINEUNIX_FAST_IO_TEST))
			set(extra_includes)
			set(extra_deps)
			if(testexe STREQUAL "fast_io_winetest")
				set(extra_includes -I${LIBWINEUNIX_FAST_IO_INCLUDE})
				set(extra_deps ${LIBWINEUNIX_FAST_IO_INCLUDE}/fast_io_hosted/platforms/wine/wine.h)
			endif()
			# test exe links the import lib; the sibling wineunix.dll is loaded
			add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${testexe}.exe
				COMMAND ${LIBWINEUNIX_PE_CXX} ${LIBWINEUNIX_PE_CXXFLAGS_LIST} -fherbceptions
					-o ${CMAKE_BINARY_DIR}/${testexe}.exe
					${CMAKE_CURRENT_SOURCE_DIR}/test/${testexe}.cc
					-I${CMAKE_CURRENT_SOURCE_DIR}/include ${extra_includes}
					${CMAKE_BINARY_DIR}/libwineunix.a -lherbceptions
				DEPENDS test/${testexe}.cc ${CMAKE_BINARY_DIR}/libwineunix.a
					${LIBWINEUNIX_ABI_HEADERS} ${extra_deps}
				COMMENT "PE exe: ${testexe}.exe")
			add_custom_target(${testexe} ALL DEPENDS ${CMAKE_BINARY_DIR}/${testexe}.exe)
		endif()
	endforeach()
endif()

if(LIBWINEUNIX_IMPORT_LIB)
	# llvm-dlltool defaults to x64 — the import lib must carry the PE machine.
	# LIBWINEUNIX_PE_MACHINE wins when set; otherwise derive it from
	# CMAKE_SYSTEM_PROCESSOR (the unixlib's arch always matches its PE side).
	if(NOT LIBWINEUNIX_PE_MACHINE)
		# a bare -DCMAKE_SYSTEM_PROCESSOR=... only lands in the cache — read it
		# there first; system detection otherwise sets the plain variable.
		set(_wineunix_pe_target "$CACHE{CMAKE_SYSTEM_PROCESSOR}")
		if(NOT _wineunix_pe_target)
			set(_wineunix_pe_target "${CMAKE_SYSTEM_PROCESSOR}")
		endif()
		string(TOLOWER "${_wineunix_pe_target}" _wineunix_pe_target)
		if(_wineunix_pe_target MATCHES "^(aarch64|arm64)$")
			set(LIBWINEUNIX_PE_MACHINE arm64)
		elseif(_wineunix_pe_target MATCHES "^(x86_64|amd64)$")
			set(LIBWINEUNIX_PE_MACHINE i386:x86-64)
		elseif(_wineunix_pe_target MATCHES "^(i[3-6]86|x86)$")
			set(LIBWINEUNIX_PE_MACHINE i386)
		elseif(_wineunix_pe_target MATCHES "^arm64ec")
			set(LIBWINEUNIX_PE_MACHINE arm64ec)
		elseif(_wineunix_pe_target MATCHES "^arm")
			set(LIBWINEUNIX_PE_MACHINE arm)
		else()
			set(LIBWINEUNIX_PE_MACHINE i386:x86-64)
		endif()
	endif()

	add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/libwineunix.a
		COMMAND ${LIBWINEUNIX_DLLTOOL} -m ${LIBWINEUNIX_PE_MACHINE}
			-d ${CMAKE_CURRENT_SOURCE_DIR}/def/wineunix.def
			-D wineunix.dll -l ${CMAKE_BINARY_DIR}/libwineunix.a
		DEPENDS def/wineunix.def
		COMMENT "import lib: libwineunix.a (${LIBWINEUNIX_PE_MACHINE})")
	add_custom_target(wineunix_import ALL DEPENDS ${CMAKE_BINARY_DIR}/libwineunix.a)
endif()

# unixcall-capable impls run under wine with the unixlib reachable (WINEDLLPATH);
# nt-capable impls also run with it unset — the dll falls back to ntdll.
if(LIBWINEUNIX_UNIXLIB AND LIBWINEUNIX_WINEUNIX_DLL AND LIBWINEUNIX_IMPORT_LIB
		AND (LIBWINEUNIX_TEST OR LIBWINEUNIX_FAST_IO_TEST))
	set(LIBWINEUNIX_CHECK_COMMANDS)
	set(LIBWINEUNIX_CHECK_DEPS wineunix wineunix_dll)
	foreach(testexe winetest fast_io_winetest)
		if((testexe STREQUAL "winetest" AND LIBWINEUNIX_TEST)
				OR (testexe STREQUAL "fast_io_winetest" AND LIBWINEUNIX_FAST_IO_TEST))
			if(NOT LIBWINEUNIX_WINEUNIX_IMPL STREQUAL "nt")
				list(APPEND LIBWINEUNIX_CHECK_COMMANDS
					COMMAND ${CMAKE_COMMAND} -E env
						WINEDLLPATH=${CMAKE_BINARY_DIR}
						${LIBWINEUNIX_WINE} ${CMAKE_BINARY_DIR}/${testexe}.exe)
			endif()
			if(NOT LIBWINEUNIX_WINEUNIX_IMPL STREQUAL "unixcall")
				list(APPEND LIBWINEUNIX_CHECK_COMMANDS
					COMMAND ${CMAKE_COMMAND} -E env
						${LIBWINEUNIX_WINE} ${CMAKE_BINARY_DIR}/${testexe}.exe)
			endif()
			list(APPEND LIBWINEUNIX_CHECK_DEPS ${testexe})
		endif()
	endforeach()
	add_custom_target(check
		${LIBWINEUNIX_CHECK_COMMANDS}
		DEPENDS ${LIBWINEUNIX_CHECK_DEPS}
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "running test exes under wine (${LIBWINEUNIX_WINEUNIX_IMPL})")
endif()

# ---------------------------------------------------------------------------
# install. The cmake host target is usually linux, so the default layout is:
#   <prefix>/lib/libwineunix.so                      (unix side)
#   <prefix>/share/wineunix/                         (windows-side prefix)
#       include/__wine_unix/*.h
#       lib/libwineunix.a                            (PE import lib)
#       bin/wineunix.dll
# LIBWINEUNIX_INSTALL_WINDOWS_DIR and LIBWINEUNIX_INSTALL_UNIX_DIR override the two
# roots — e.g. point the unix dir at wine's unixlib dir (lib/wine/x86_64-unix)
# or any WINEDLLPATH dir when running under emulation.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

set(LIBWINEUNIX_INSTALL_WINDOWS_DIR "${CMAKE_INSTALL_DATADIR}/wineunix"
	CACHE PATH "windows-side install prefix (bin/ dlls, lib/ import lib, include/ headers)")
set(LIBWINEUNIX_INSTALL_UNIX_DIR "${CMAKE_INSTALL_LIBDIR}"
	CACHE PATH "install dir for libwineunix.so (a wine unixlib/WINEDLLPATH dir)")

install(DIRECTORY include/__wine_unix DESTINATION ${LIBWINEUNIX_INSTALL_WINDOWS_DIR}/include)

if(TARGET wineunix)
	install(TARGETS wineunix LIBRARY DESTINATION ${LIBWINEUNIX_INSTALL_UNIX_DIR})
endif()

if(LIBWINEUNIX_WINEUNIX_DLL)
	install(FILES ${CMAKE_BINARY_DIR}/wineunix.dll
		DESTINATION ${LIBWINEUNIX_INSTALL_WINDOWS_DIR}/bin)
endif()

if(LIBWINEUNIX_IMPORT_LIB)
	# the import lib ships under each windows linker's spelling —
	# wineunix.lib (msvc/lld-link) and libwineunix.dll.a (mingw-w64) — in the
	# windows-side lib dir, plus its plain name in the unix libdir.
	install(FILES ${CMAKE_BINARY_DIR}/libwineunix.a
		RENAME wineunix.lib
		DESTINATION ${LIBWINEUNIX_INSTALL_WINDOWS_DIR}/lib)
	install(FILES ${CMAKE_BINARY_DIR}/libwineunix.a
		RENAME libwineunix.dll.a
		DESTINATION ${LIBWINEUNIX_INSTALL_WINDOWS_DIR}/lib)
	install(FILES ${CMAKE_BINARY_DIR}/libwineunix.a
		DESTINATION ${LIBWINEUNIX_INSTALL_UNIX_DIR})
endif()
