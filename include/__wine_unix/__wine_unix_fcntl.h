#pragma once
/*
Wire flag values for the __wine_unix_openat unixcall.

The unixcall ABI needs flag values fixed across hosts, but the PE side cannot
include host headers. The errno contract in __wine_unix_errno.h is already the
linux errno set, so these are the linux x86-64 flag values; unixhost.cc
translates them to the host's native open() flags bit by bit.
*/

#define __WINE_UNIX_O_ACCMODE 3
#define __WINE_UNIX_O_RDONLY 0
#define __WINE_UNIX_O_WRONLY 1
#define __WINE_UNIX_O_RDWR 2

#define __WINE_UNIX_O_CREAT 0x40
#define __WINE_UNIX_O_EXCL 0x80
#define __WINE_UNIX_O_NOCTTY 0x100
#define __WINE_UNIX_O_TRUNC 0x200
#define __WINE_UNIX_O_APPEND 0x400
#define __WINE_UNIX_O_NONBLOCK 0x800
#define __WINE_UNIX_O_DSYNC 0x1000
#define __WINE_UNIX_O_DIRECT 0x4000
#define __WINE_UNIX_O_LARGEFILE 0x8000
#define __WINE_UNIX_O_DIRECTORY 0x10000
#define __WINE_UNIX_O_NOFOLLOW 0x20000
#define __WINE_UNIX_O_NOATIME 0x40000
#define __WINE_UNIX_O_CLOEXEC 0x80000
#define __WINE_UNIX_O_SYNC 0x101000
#define __WINE_UNIX_O_PATH 0x200000
#define __WINE_UNIX_O_TMPFILE (0x410000)
