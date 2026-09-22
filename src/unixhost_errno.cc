/*
non-linux host errno -> __WINE_UNIX_ERRNO_* mapping. Out-of-line so the big
switch doesn't get inlined into every call site of unixhost.cc.
*/
#include "__wine_unix_abi.h"
#include <__wine_unix/__wine_unix_errno.h>
#include <errno.h>

#if !defined(__linux__)
namespace __wine_unix
{
__wine_unix_status_t host_errno_to_wine_errno(int val) noexcept
{
	switch (val)
	{
	case 0:
		return __WINE_UNIX_ERRNO_SUCCESS;
#ifdef E2BIG
	case E2BIG:
		return __WINE_UNIX_ERRNO_E2BIG;
#endif
#ifdef EACCES
	case EACCES:
		return __WINE_UNIX_ERRNO_EACCES;
#endif
#ifdef EADDRINUSE
	case EADDRINUSE:
		return __WINE_UNIX_ERRNO_EADDRINUSE;
#endif
#ifdef EADDRNOTAVAIL
	case EADDRNOTAVAIL:
		return __WINE_UNIX_ERRNO_EADDRNOTAVAIL;
#endif
#ifdef EADV
	case EADV:
		return __WINE_UNIX_ERRNO_EADV;
#endif
#ifdef EAFNOSUPPORT
	case EAFNOSUPPORT:
		return __WINE_UNIX_ERRNO_EAFNOSUPPORT;
#endif
#ifdef EAGAIN
	case EAGAIN:
		return __WINE_UNIX_ERRNO_EAGAIN;
#endif
#ifdef EALREADY
	case EALREADY:
		return __WINE_UNIX_ERRNO_EALREADY;
#endif
#ifdef EBADE
	case EBADE:
		return __WINE_UNIX_ERRNO_EBADE;
#endif
#ifdef EBADF
	case EBADF:
		return __WINE_UNIX_ERRNO_EBADF;
#endif
#ifdef EBADFD
	case EBADFD:
		return __WINE_UNIX_ERRNO_EBADFD;
#endif
#ifdef EBADMSG
	case EBADMSG:
		return __WINE_UNIX_ERRNO_EBADMSG;
#endif
#ifdef EBADR
	case EBADR:
		return __WINE_UNIX_ERRNO_EBADR;
#endif
#ifdef EBADRQC
	case EBADRQC:
		return __WINE_UNIX_ERRNO_EBADRQC;
#endif
#ifdef EBADSLT
	case EBADSLT:
		return __WINE_UNIX_ERRNO_EBADSLT;
#endif
#ifdef EBFONT
	case EBFONT:
		return __WINE_UNIX_ERRNO_EBFONT;
#endif
#ifdef EBUSY
	case EBUSY:
		return __WINE_UNIX_ERRNO_EBUSY;
#endif
#ifdef ECANCELED
	case ECANCELED:
		return __WINE_UNIX_ERRNO_ECANCELED;
#endif
#ifdef ECHILD
	case ECHILD:
		return __WINE_UNIX_ERRNO_ECHILD;
#endif
#ifdef ECHRNG
	case ECHRNG:
		return __WINE_UNIX_ERRNO_ECHRNG;
#endif
#ifdef ECOMM
	case ECOMM:
		return __WINE_UNIX_ERRNO_ECOMM;
#endif
#ifdef ECONNABORTED
	case ECONNABORTED:
		return __WINE_UNIX_ERRNO_ECONNABORTED;
#endif
#ifdef ECONNREFUSED
	case ECONNREFUSED:
		return __WINE_UNIX_ERRNO_ECONNREFUSED;
#endif
#ifdef ECONNRESET
	case ECONNRESET:
		return __WINE_UNIX_ERRNO_ECONNRESET;
#endif
#ifdef EDEADLK
	case EDEADLK:
		return __WINE_UNIX_ERRNO_EDEADLK;
#endif
#ifdef EDESTADDRREQ
	case EDESTADDRREQ:
		return __WINE_UNIX_ERRNO_EDESTADDRREQ;
#endif
#ifdef EDOM
	case EDOM:
		return __WINE_UNIX_ERRNO_EDOM;
#endif
#ifdef EDOTDOT
	case EDOTDOT:
		return __WINE_UNIX_ERRNO_EDOTDOT;
#endif
#ifdef EDQUOT
	case EDQUOT:
		return __WINE_UNIX_ERRNO_EDQUOT;
#endif
#ifdef EEXIST
	case EEXIST:
		return __WINE_UNIX_ERRNO_EEXIST;
#endif
#ifdef EFAULT
	case EFAULT:
		return __WINE_UNIX_ERRNO_EFAULT;
#endif
#ifdef EFBIG
	case EFBIG:
		return __WINE_UNIX_ERRNO_EFBIG;
#endif
#ifdef EHOSTDOWN
	case EHOSTDOWN:
		return __WINE_UNIX_ERRNO_EHOSTDOWN;
#endif
#ifdef EHOSTUNREACH
	case EHOSTUNREACH:
		return __WINE_UNIX_ERRNO_EHOSTUNREACH;
#endif
#ifdef EHWPOISON
	case EHWPOISON:
		return __WINE_UNIX_ERRNO_EHWPOISON;
#endif
#ifdef EIDRM
	case EIDRM:
		return __WINE_UNIX_ERRNO_EIDRM;
#endif
#ifdef EILSEQ
	case EILSEQ:
		return __WINE_UNIX_ERRNO_EILSEQ;
#endif
#ifdef EINPROGRESS
	case EINPROGRESS:
		return __WINE_UNIX_ERRNO_EINPROGRESS;
#endif
#ifdef EINTR
	case EINTR:
		return __WINE_UNIX_ERRNO_EINTR;
#endif
#ifdef EINVAL
	case EINVAL:
		return __WINE_UNIX_ERRNO_EINVAL;
#endif
#ifdef EIO
	case EIO:
		return __WINE_UNIX_ERRNO_EIO;
#endif
#ifdef EISCONN
	case EISCONN:
		return __WINE_UNIX_ERRNO_EISCONN;
#endif
#ifdef EISDIR
	case EISDIR:
		return __WINE_UNIX_ERRNO_EISDIR;
#endif
#ifdef EISNAM
	case EISNAM:
		return __WINE_UNIX_ERRNO_EISNAM;
#endif
#ifdef EKEYEXPIRED
	case EKEYEXPIRED:
		return __WINE_UNIX_ERRNO_EKEYEXPIRED;
#endif
#ifdef EKEYREJECTED
	case EKEYREJECTED:
		return __WINE_UNIX_ERRNO_EKEYREJECTED;
#endif
#ifdef EKEYREVOKED
	case EKEYREVOKED:
		return __WINE_UNIX_ERRNO_EKEYREVOKED;
#endif
#ifdef EL2HLT
	case EL2HLT:
		return __WINE_UNIX_ERRNO_EL2HLT;
#endif
#ifdef EL2NSYNC
	case EL2NSYNC:
		return __WINE_UNIX_ERRNO_EL2NSYNC;
#endif
#ifdef EL3HLT
	case EL3HLT:
		return __WINE_UNIX_ERRNO_EL3HLT;
#endif
#ifdef EL3RST
	case EL3RST:
		return __WINE_UNIX_ERRNO_EL3RST;
#endif
#ifdef ELIBACC
	case ELIBACC:
		return __WINE_UNIX_ERRNO_ELIBACC;
#endif
#ifdef ELIBBAD
	case ELIBBAD:
		return __WINE_UNIX_ERRNO_ELIBBAD;
#endif
#ifdef ELIBEXEC
	case ELIBEXEC:
		return __WINE_UNIX_ERRNO_ELIBEXEC;
#endif
#ifdef ELIBMAX
	case ELIBMAX:
		return __WINE_UNIX_ERRNO_ELIBMAX;
#endif
#ifdef ELIBSCN
	case ELIBSCN:
		return __WINE_UNIX_ERRNO_ELIBSCN;
#endif
#ifdef ELNRNG
	case ELNRNG:
		return __WINE_UNIX_ERRNO_ELNRNG;
#endif
#ifdef ELOOP
	case ELOOP:
		return __WINE_UNIX_ERRNO_ELOOP;
#endif
#ifdef EMEDIUMTYPE
	case EMEDIUMTYPE:
		return __WINE_UNIX_ERRNO_EMEDIUMTYPE;
#endif
#ifdef EMFILE
	case EMFILE:
		return __WINE_UNIX_ERRNO_EMFILE;
#endif
#ifdef EMLINK
	case EMLINK:
		return __WINE_UNIX_ERRNO_EMLINK;
#endif
#ifdef EMSGSIZE
	case EMSGSIZE:
		return __WINE_UNIX_ERRNO_EMSGSIZE;
#endif
#ifdef EMULTIHOP
	case EMULTIHOP:
		return __WINE_UNIX_ERRNO_EMULTIHOP;
#endif
#ifdef ENAMETOOLONG
	case ENAMETOOLONG:
		return __WINE_UNIX_ERRNO_ENAMETOOLONG;
#endif
#ifdef ENAVAIL
	case ENAVAIL:
		return __WINE_UNIX_ERRNO_ENAVAIL;
#endif
#ifdef ENETDOWN
	case ENETDOWN:
		return __WINE_UNIX_ERRNO_ENETDOWN;
#endif
#ifdef ENETRESET
	case ENETRESET:
		return __WINE_UNIX_ERRNO_ENETRESET;
#endif
#ifdef ENETUNREACH
	case ENETUNREACH:
		return __WINE_UNIX_ERRNO_ENETUNREACH;
#endif
#ifdef ENFILE
	case ENFILE:
		return __WINE_UNIX_ERRNO_ENFILE;
#endif
#ifdef ENOANO
	case ENOANO:
		return __WINE_UNIX_ERRNO_ENOANO;
#endif
#ifdef ENOBUFS
	case ENOBUFS:
		return __WINE_UNIX_ERRNO_ENOBUFS;
#endif
#ifdef ENOCSI
	case ENOCSI:
		return __WINE_UNIX_ERRNO_ENOCSI;
#endif
#ifdef ENODATA
	case ENODATA:
		return __WINE_UNIX_ERRNO_ENODATA;
#endif
#ifdef ENODEV
	case ENODEV:
		return __WINE_UNIX_ERRNO_ENODEV;
#endif
#ifdef ENOENT
	case ENOENT:
		return __WINE_UNIX_ERRNO_ENOENT;
#endif
#ifdef ENOEXEC
	case ENOEXEC:
		return __WINE_UNIX_ERRNO_ENOEXEC;
#endif
#ifdef ENOKEY
	case ENOKEY:
		return __WINE_UNIX_ERRNO_ENOKEY;
#endif
#ifdef ENOLCK
	case ENOLCK:
		return __WINE_UNIX_ERRNO_ENOLCK;
#endif
#ifdef ENOLINK
	case ENOLINK:
		return __WINE_UNIX_ERRNO_ENOLINK;
#endif
#ifdef ENOMEDIUM
	case ENOMEDIUM:
		return __WINE_UNIX_ERRNO_ENOMEDIUM;
#endif
#ifdef ENOMEM
	case ENOMEM:
		return __WINE_UNIX_ERRNO_ENOMEM;
#endif
#ifdef ENOMSG
	case ENOMSG:
		return __WINE_UNIX_ERRNO_ENOMSG;
#endif
#ifdef ENONET
	case ENONET:
		return __WINE_UNIX_ERRNO_ENONET;
#endif
#ifdef ENOPKG
	case ENOPKG:
		return __WINE_UNIX_ERRNO_ENOPKG;
#endif
#ifdef ENOPROTOOPT
	case ENOPROTOOPT:
		return __WINE_UNIX_ERRNO_ENOPROTOOPT;
#endif
#ifdef ENOSPC
	case ENOSPC:
		return __WINE_UNIX_ERRNO_ENOSPC;
#endif
#ifdef ENOSR
	case ENOSR:
		return __WINE_UNIX_ERRNO_ENOSR;
#endif
#ifdef ENOSTR
	case ENOSTR:
		return __WINE_UNIX_ERRNO_ENOSTR;
#endif
#ifdef ENOSYS
	case ENOSYS:
		return __WINE_UNIX_ERRNO_ENOSYS;
#endif
#ifdef ENOTBLK
	case ENOTBLK:
		return __WINE_UNIX_ERRNO_ENOTBLK;
#endif
#ifdef ENOTCONN
	case ENOTCONN:
		return __WINE_UNIX_ERRNO_ENOTCONN;
#endif
#ifdef ENOTDIR
	case ENOTDIR:
		return __WINE_UNIX_ERRNO_ENOTDIR;
#endif
#ifdef ENOTEMPTY
	case ENOTEMPTY:
		return __WINE_UNIX_ERRNO_ENOTEMPTY;
#endif
#ifdef ENOTNAM
	case ENOTNAM:
		return __WINE_UNIX_ERRNO_ENOTNAM;
#endif
#ifdef ENOTRECOVERABLE
	case ENOTRECOVERABLE:
		return __WINE_UNIX_ERRNO_ENOTRECOVERABLE;
#endif
#ifdef ENOTSOCK
	case ENOTSOCK:
		return __WINE_UNIX_ERRNO_ENOTSOCK;
#endif
#ifdef ENOTTY
	case ENOTTY:
		return __WINE_UNIX_ERRNO_ENOTTY;
#endif
#ifdef ENOTUNIQ
	case ENOTUNIQ:
		return __WINE_UNIX_ERRNO_ENOTUNIQ;
#endif
#ifdef ENXIO
	case ENXIO:
		return __WINE_UNIX_ERRNO_ENXIO;
#endif
#ifdef EOPNOTSUPP
	case EOPNOTSUPP:
		return __WINE_UNIX_ERRNO_EOPNOTSUPP;
#endif
#ifdef EOVERFLOW
	case EOVERFLOW:
		return __WINE_UNIX_ERRNO_EOVERFLOW;
#endif
#ifdef EOWNERDEAD
	case EOWNERDEAD:
		return __WINE_UNIX_ERRNO_EOWNERDEAD;
#endif
#ifdef EPERM
	case EPERM:
		return __WINE_UNIX_ERRNO_EPERM;
#endif
#ifdef EPFNOSUPPORT
	case EPFNOSUPPORT:
		return __WINE_UNIX_ERRNO_EPFNOSUPPORT;
#endif
#ifdef EPIPE
	case EPIPE:
		return __WINE_UNIX_ERRNO_EPIPE;
#endif
#ifdef EPROTO
	case EPROTO:
		return __WINE_UNIX_ERRNO_EPROTO;
#endif
#ifdef EPROTONOSUPPORT
	case EPROTONOSUPPORT:
		return __WINE_UNIX_ERRNO_EPROTONOSUPPORT;
#endif
#ifdef EPROTOTYPE
	case EPROTOTYPE:
		return __WINE_UNIX_ERRNO_EPROTOTYPE;
#endif
#ifdef ERANGE
	case ERANGE:
		return __WINE_UNIX_ERRNO_ERANGE;
#endif
#ifdef EREMCHG
	case EREMCHG:
		return __WINE_UNIX_ERRNO_EREMCHG;
#endif
#ifdef EREMOTE
	case EREMOTE:
		return __WINE_UNIX_ERRNO_EREMOTE;
#endif
#ifdef EREMOTEIO
	case EREMOTEIO:
		return __WINE_UNIX_ERRNO_EREMOTEIO;
#endif
#ifdef ERESTART
	case ERESTART:
		return __WINE_UNIX_ERRNO_ERESTART;
#endif
#ifdef ERFKILL
	case ERFKILL:
		return __WINE_UNIX_ERRNO_ERFKILL;
#endif
#ifdef EROFS
	case EROFS:
		return __WINE_UNIX_ERRNO_EROFS;
#endif
#ifdef ESHUTDOWN
	case ESHUTDOWN:
		return __WINE_UNIX_ERRNO_ESHUTDOWN;
#endif
#ifdef ESOCKTNOSUPPORT
	case ESOCKTNOSUPPORT:
		return __WINE_UNIX_ERRNO_ESOCKTNOSUPPORT;
#endif
#ifdef ESPIPE
	case ESPIPE:
		return __WINE_UNIX_ERRNO_ESPIPE;
#endif
#ifdef ESRCH
	case ESRCH:
		return __WINE_UNIX_ERRNO_ESRCH;
#endif
#ifdef ESRMNT
	case ESRMNT:
		return __WINE_UNIX_ERRNO_ESRMNT;
#endif
#ifdef ESTALE
	case ESTALE:
		return __WINE_UNIX_ERRNO_ESTALE;
#endif
#ifdef ESTRPIPE
	case ESTRPIPE:
		return __WINE_UNIX_ERRNO_ESTRPIPE;
#endif
#ifdef ETIME
	case ETIME:
		return __WINE_UNIX_ERRNO_ETIME;
#endif
#ifdef ETIMEDOUT
	case ETIMEDOUT:
		return __WINE_UNIX_ERRNO_ETIMEDOUT;
#endif
#ifdef ETOOMANYREFS
	case ETOOMANYREFS:
		return __WINE_UNIX_ERRNO_ETOOMANYREFS;
#endif
#ifdef ETXTBSY
	case ETXTBSY:
		return __WINE_UNIX_ERRNO_ETXTBSY;
#endif
#ifdef EUCLEAN
	case EUCLEAN:
		return __WINE_UNIX_ERRNO_EUCLEAN;
#endif
#ifdef EUNATCH
	case EUNATCH:
		return __WINE_UNIX_ERRNO_EUNATCH;
#endif
#ifdef EUSERS
	case EUSERS:
		return __WINE_UNIX_ERRNO_EUSERS;
#endif
#ifdef EXDEV
	case EXDEV:
		return __WINE_UNIX_ERRNO_EXDEV;
#endif
#ifdef EXFULL
	case EXFULL:
		return __WINE_UNIX_ERRNO_EXFULL;
#endif
#if defined(EWOULDBLOCK) && (!defined(EAGAIN) || EWOULDBLOCK != EAGAIN)
	case EWOULDBLOCK:
		return __WINE_UNIX_ERRNO_EWOULDBLOCK;
#endif
#if defined(EDEADLOCK) && (!defined(EDEADLK) || EDEADLOCK != EDEADLK)
	case EDEADLOCK:
		return __WINE_UNIX_ERRNO_EDEADLOCK;
#endif
#if defined(ENOTSUP) && (!defined(EOPNOTSUPP) || ENOTSUP != EOPNOTSUPP)
	case ENOTSUP:
		return __WINE_UNIX_ERRNO_EOPNOTSUPP;
#endif
	default:
		return __WINE_UNIX_ERRNO_EINVAL;
	}
}

} // namespace __wine_unix
#endif
