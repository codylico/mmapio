/*
 * \file mmapio.c
 * \brief Memory-mapped files
 * \author Cody Licorish (svgmovement@gmail.com)
 */
#define MMAPIO_WIN32_DLL_INTERNAL
#define _POSIX_C_SOURCE 200809L
#include "mmapio.h"
#include <stdlib.h>

#ifndef MMAPIO_MAX_CACHE
#  define MMAPIO_MAX_CACHE 1048576
#endif /*MMAPIO_MAX_CACHE*/

/**
 * \brief Mode tag for `mmapio` interface, holding various
 *   mapping configuration values.
 */
struct mmapio_mode_tag {
  /** \brief access mode (readonly, read-write) */
  char mode;
  /** \brief map-to-end-of-file flag */
  char end;
  /** \brief flag for activating private mapping */
  char privy;
  /** \brief flag for enabling access from child processes */
  char bequeath;
};

/**
 * \brief Extract a `mmapio` mode tag from a mode text.
 * \param mmode the text to parse
 * \return a `mmapio` mode tag
 */
static struct mmapio_mode_tag mmapio_mode_parse(char const* mmode);

#define MMAPIO_OS_UNIX 1
#define MMAPIO_OS_WIN32 2

/*
 * inspired by https://stackoverflow.com/a/30971057
 * and https://stackoverflow.com/a/11351171
 */
#ifndef MMAPIO_OS
#  if (defined _WIN32)
#    define MMAPIO_OS MMAPIO_OS_WIN32
#  elif (defined __unix__) || (defined(__APPLE__)&&defined(__MACH__))
#    define MMAPIO_OS MMAPIO_OS_UNIX
#  else
#    define MMAPIO_OS 0
#  endif
#endif /*MMAPIO_OS*/

#if MMAPIO_OS == MMAPIO_OS_UNIX
#  include <unistd.h>
#if (defined __STDC_VERSION__) && (__STDC_VERSION__ >= 199501L)
#  include <wchar.h>
#  include <string.h>
#endif /*__STDC_VERSION__*/
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <errno.h>

/**
 * \brief Structure for POSIX `mmapio` implementation.
 */
struct mmapio_unix {
  /** \brief base structure */
  struct mmapio_i base;
  /** \brief `mmap` pointer */
  void* ptr;
  /** \brief length of space */
  size_t len;
  /** \brief offset from `ptr` to start of user-requested space */
  size_t shift;
  /** \brief file descriptor */
  int fd;
};

/**
 * \brief Convert a wide string to a multibyte string.
 * \param nm the string to convert
 * \return a multibyte string on success, NULL otherwise
 */
static char* mmapio_wctomb(wchar_t const* nm);

/**
 * \brief Convert a `mmapio` mode character to a POSIX `open` flag.
 * \param mmode the character to convert
 * \return an `open` flag on success, zero otherwise
 */
static int mmapio_mode_rw_cvt(int mmode);

/**
 * \brief Convert a `mmapio` mode character to a POSIX `mmap` protection flag.
 * \param mmode the character to convert
 * \return an `mmap` protection flag on success, zero otherwise
 */
static int mmapio_mode_prot_cvt(int mmode);

/**
 * \brief Convert a `mmapio` mode character to a POSIX `mmap` others' flag.
 * \param mprivy the private flag to convert
 * \return an `mmap` others' flag on success, zero otherwise
 */
static int mmapio_mode_flag_cvt(int mprivy);

/**
 * \brief Fetch a file size from a file descriptor.
 * \param fd target file descriptor
 * \return a file size, or zero on failure
 */
static size_t mmapio_file_size_e(int fd);

/**
 * \brief Finish preparing a memory map interface.
 * \param fd file descriptor
 * \param mmode mode tag
 * \param sz size of range to map
 * \param off offset from start of file
 * \return an interface on success, NULL otherwise
 */
static struct mmapio_i* mmapio_open_rest
  (int fd, struct mmapio_mode_tag const mmode, size_t sz, size_t off);

/**
 * \brief Destructor; closes the file and frees the space.
 * \param m map instance
 */
static void mmapio_mmi_dtor(struct mmapio_i* m);

/**
 * \brief Acquire a lock to the space.
 * \param m map instance
 * \return pointer to locked space on success, NULL otherwise
 */
static void* mmapio_mmi_acquire(struct mmapio_i* m);

/**
 * \brief Release a lock of the space.
 * \param m map instance
 * \param p pointer of region to release
 */
static void mmapio_mmi_release(struct mmapio_i* m, void* p);

/**
 * \brief Check the length of the mapped area.
 * \param m map instance
 * \return the length of the mapped region exposed by this interface
 */
static size_t mmapio_mmi_length(struct mmapio_i const* m);
#elif MMAPIO_OS == MMAPIO_OS_WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <limits.h>
#  include <errno.h>
#  ifdef EILSEQ
#    define MMAPIO_EILSEQ EILSEQ
#  else
#    define MMAPIO_EILSEQ EDOM
#  endif /*EILSEQ*/

/**
 * \brief Structure for Win32 API `mmapio` implementation.
 */
struct mmapio_win32 {
  /** \brief base structure */
  struct mmapio_i base;
  /** \brief `MapViewOfFile` pointer */
  void* ptr;
  /** \brief length of space */
  size_t len;
  /** \brief offset from `ptr` to start of user-requested space */
  size_t shift;
  /** \brief file mapping handle */
  HANDLE fmd;
  /** \brief file handle */
  HANDLE fd;
};

/**
 * \brief Convert a `mmapio` mode character to a `CreateFile.`
 *   desired access flag.
 * \param mmode the character to convert
 * \return a `CreateFile.` desired access flag on success, zero otherwise
 */
static DWORD mmapio_mode_rw_cvt(int mmode);

/**
 * \brief Convert UTF-8 encoded text to UTF-16 LE text.
 * \param nm file name encoded in UTF-8
 * \param[out] out output string
 * \param[out] outlen output length
 * \return an errno code
 */
static int mmapio_u8towc_shim
  (unsigned char const* nm, wchar_t* out, size_t* outlen);

/**
 * \brief Convert UTF-8 encoded text to UTF-16 LE text.
 * \param nm file name encoded in UTF-8
 * \return a wide string on success, NULL otherwise
 */
static wchar_t* mmapio_u8towc(unsigned char const* nm);

/**
 * \brief Finish preparing a memory map interface.
 * \param fd file handle
 * \param mmode mode tag
 * \param sz size of range to map
 * \param off offset from start of file
 * \return an interface on success, NULL otherwise
 */
static struct mmapio_i* mmapio_open_rest
  (HANDLE fd, struct mmapio_mode_tag const mmode, size_t sz, size_t off);

/**
 * \brief Fetch a file size from a file descriptor.
 * \param fd target file handle
 * \return a file size, or zero on failure
 */
static size_t mmapio_file_size_e(HANDLE fd);

/**
 * \brief Convert a `mmapio` mode character to a
 *   `CreateFileMapping.` protection flag.
 * \param mmode the character to convert
 * \return a `CreateFileMapping.` protection flag on success, zero otherwise
 */
static DWORD mmapio_mode_prot_cvt(int mmode);

/**
 * \brief Convert a `mmapio` mode tag to a `MapViewOfFile`
 *   desired access flag.
 * \param mmode the tag to convert
 * \return a `MapViewOfFile` desired access flag on success, zero otherwise
 */
static DWORD mmapio_mode_access_cvt(struct mmapio_mode_tag const mt);

/**
 * \brief Destructor; closes the file and frees the space.
 * \param m map instance
 */
static void mmapio_mmi_dtor(struct mmapio_i* m);

/**
 * \brief Acquire a lock to the space.
 * \param m map instance
 * \return pointer to locked space on success, NULL otherwise
 */
static void* mmapio_mmi_acquire(struct mmapio_i* m);

/**
 * \brief Release a lock of the space.
 * \param m map instance
 * \param p pointer of region to release
 */
static void mmapio_mmi_release(struct mmapio_i* m, void* p);

/**
 * \brief Check the length of the mapped area.
 * \param m map instance
 * \return the length of the mapped region exposed by this interface
 */
static size_t mmapio_mmi_length(struct mmapio_i const* m);
#endif /*MMAPIO_OS*/

/* BEGIN static functions */
struct mmapio_mode_tag mmapio_mode_parse(char const* mmode) {
  struct mmapio_mode_tag out = { 0, 0, 0, 0 };
  int i;
  for (i = 0; i < 8; ++i) {
    switch (mmode[i]) {
    case 0: /* NUL termination */
      return out;
    case mmapio_mode_write:
      out.mode = mmapio_mode_write;
      break;
    case mmapio_mode_read:
      out.mode = mmapio_mode_read;
      break;
    case mmapio_mode_end:
      out.end = mmapio_mode_end;
      break;
    case mmapio_mode_private:
      out.privy = mmapio_mode_private;
      break;
    case mmapio_mode_bequeath:
      out.bequeath = mmapio_mode_bequeath;
      break;
    }
  }
  return out;
}

#if MMAPIO_OS == MMAPIO_OS_UNIX
char* mmapio_wctomb(wchar_t const* nm) {
#if (defined __STDC_VERSION__) && (__STDC_VERSION__ >= 199409L)
  /* use multibyte conversion */
  size_t ns;
  char* out;
  /* try the length */{
    mbstate_t mbs;
    wchar_t const* test_nm = nm;
    memset(&mbs, 0, sizeof(mbs));
    ns = wcsrtombs(NULL, &test_nm, 0, &mbs);
  }
  if (ns == (size_t)(-1)
  ||  ns == ~((size_t)0))
  {
    /* conversion error caused by bad sequence, so */return NULL;
  }
  out = calloc(ns+1, sizeof(char));
  if (out) {
    mbstate_t mbs;
    wchar_t const* test_nm = nm;
    memset(&mbs, 0, sizeof(mbs));
    wcsrtombs(out, &test_nm, ns+1, &mbs);
    out[ns] = 0;
  }
  return out;
#else
  /* no thread-safe version, so give up */
  return NULL;
#endif /*__STDC_VERSION__*/
}

int mmapio_mode_rw_cvt(int mmode) {
#if (defined O_CLOEXEC)
  int const fast_no_bequeath = (int)(O_CLOEXEC);
#else
  int const fast_no_bequeath = 0;
#endif /*O_CLOEXEC*/
  switch (mmode) {
  case mmapio_mode_write:
    return O_RDWR|fast_no_bequeath;
  case mmapio_mode_read:
    return O_RDONLY|fast_no_bequeath;
  default:
    return 0;
  }
}

int mmapio_mode_prot_cvt(int mmode) {
  switch (mmode) {
  case mmapio_mode_write:
    return PROT_WRITE|PROT_READ;
  case mmapio_mode_read:
    return PROT_READ;
  default:
    return 0;
  }
}

int mmapio_mode_flag_cvt(int mprivy) {
  return mprivy ? MAP_PRIVATE : MAP_SHARED;
}

size_t mmapio_file_size_e(int fd) {
  struct stat fsi;
  memset(&fsi, 0, sizeof(fsi));
  /* stat pull */{
    int const res = fstat(fd, &fsi);
    if (res != 0) {
      return 0u;
    } else return (size_t)(fsi.st_size);
  }
}

struct mmapio_i* mmapio_open_rest
  (int fd, struct mmapio_mode_tag const mt, size_t sz, size_t off)
{
  struct mmapio_unix *const out = calloc(1, sizeof(struct mmapio_unix));
  void *ptr;
  size_t fullsize;
  size_t fullshift;
  off_t fulloff;
  if (out == NULL) {
    close(fd);
    return NULL;
  }
  /* assign the close-on-exec flag */{
    int const old_flags = fcntl(fd, F_GETFD);
    int bequeath_break = 0;
    if (old_flags < 0) {
      bequeath_break = 1;
    } else if (mt.bequeath) {
      bequeath_break = (fcntl(fd, F_SETFD, old_flags&(~FD_CLOEXEC)) < 0);
    } else {
      bequeath_break = (fcntl(fd, F_SETFD, old_flags|FD_CLOEXEC) < 0);
    }
    if (bequeath_break) {
      close(fd);
      free(out);
      return NULL;
    }
  }
  if (mt.end) /* fix map size */{
    size_t const xsz = mmapio_file_size_e(fd);
    if (xsz < off)
      sz = 0 /*to fail*/;
    else sz = xsz-off;
  }
  /* fix to page sizes */{
    long const psize = sysconf(_SC_PAGE_SIZE);
    fullsize = sz;
    if (psize > 0) {
      /* adjust the offset */
      fullshift = off%((unsigned long)psize);
      fulloff = (off_t)(off-fullshift);
      if (fullshift >= ((~(size_t)0u)-sz)) {
        /* range fix failure */
        close(fd);
        free(out);
        errno = ERANGE;
        return NULL;
      } else fullsize += fullshift;
    } else {
      fullshift = 0u;
      fulloff = (off_t)off;
    }
  }
  ptr = mmap(NULL, fullsize, mmapio_mode_prot_cvt(mt.mode),
       mmapio_mode_flag_cvt(mt.privy), fd, fulloff);
  if (ptr == NULL) {
    close(fd);
    free(out);
    return NULL;
  }
  /* initialize the interface */{
    out->ptr = ptr;
    out->len = fullsize;
    out->fd = fd;
    out->shift = fullshift;
    out->base.mmi_dtor = &mmapio_mmi_dtor;
    out->base.mmi_acquire = &mmapio_mmi_acquire;
    out->base.mmi_release = &mmapio_mmi_release;
    out->base.mmi_length = &mmapio_mmi_length;
  }
  return (struct mmapio_i*)out;
}

void mmapio_mmi_dtor(struct mmapio_i* m) {
  struct mmapio_unix* const mu = (struct mmapio_unix*)m;
  munmap(mu->ptr, mu->len);
  mu->ptr = NULL;
  close(mu->fd);
  mu->fd = -1;
  free(mu);
  return;
}

void* mmapio_mmi_acquire(struct mmapio_i* m) {
  struct mmapio_unix* const mu = (struct mmapio_unix*)m;
  return ((unsigned char*)mu->ptr)+mu->shift;
}

void mmapio_mmi_release(struct mmapio_i* m, void* p) {
  return;
}

size_t mmapio_mmi_length(struct mmapio_i const* m) {
  struct mmapio_unix* const mu = (struct mmapio_unix*)m;
  return mu->len-mu->shift;
}
#elif MMAPIO_OS == MMAPIO_OS_WIN32
DWORD mmapio_mode_rw_cvt(int mmode) {
  switch (mmode) {
  case mmapio_mode_write:
    return GENERIC_READ|GENERIC_WRITE;
  case mmapio_mode_read:
    return GENERIC_READ;
  default:
    return 0;
  }
}

int mmapio_u8towc_shim
  (unsigned char const* nm, wchar_t* out, size_t* outlen)
{
  size_t n = 0;
  unsigned char const* p;
  static size_t const sz_max = UINT_MAX/2u-4u;
  for (p = nm; *p && n < sz_max; ++p) {
    unsigned char const v = *p;
    if (n >= sz_max) {
      return ERANGE;
    }
    if (v < 0x80) {
      /* Latin-1 compatibility */
      if (out != NULL) {
        out[n] = v;
      }
      n += 1;
    } else if (v < 0xC0) {
      return MMAPIO_EILSEQ;
    } else if (v < 0xE0) {
      /* check extension codes */
      unsigned int i;
      unsigned long int qv = v&31;
      for (i = 0; i < 1; ++i) {
        unsigned char const v1 = *(p+i);
        if (v1 < 0x80 || v1 >= 0xC0) {
          return MMAPIO_EILSEQ;
        } else qv = (qv<<6)|(v1&63);
      }
      if (out != NULL) {
        out[n] = (wchar_t)qv;
      }
      n += 1;
      p += 1;
    } else if (v < 0xF0) {
      /* check extension codes */
      unsigned int i;
      unsigned long int qv = v&15;
      for (i = 0; i < 2; ++i) {
        unsigned char const v1 = *(p+i);
        if (v1 < 0x80 || v1 >= 0xC0) {
          return MMAPIO_EILSEQ;
        } else qv = (qv<<6)|(v1&63);
      }
      if (out != NULL) {
        out[n] = (wchar_t)qv;
      }
      n += 1;
      p += 2;
    } else if (v < 0xF8) {
      /* check extension codes */
      unsigned int i;
      unsigned long int qv = v&3;
      for (i = 0; i < 3; ++i) {
        unsigned char const v1 = *(p+i);
        if (v1 < 0x80 || v1 >= 0xC0) {
          return MMAPIO_EILSEQ;
        } else qv = (qv<<6)|(v1&63);
      }
      if (qv >= 0x10FFFFL) {
        return MMAPIO_EILSEQ;
      }
      if (out != NULL) {
        qv -= 0x10000;
        out[n] = (wchar_t)(0xD800 | ((qv>>10)&1023));
        out[n+1] = (wchar_t)(0xDC00 | (qv&1023));
      }
      n += 2;
      p += 3;
    } else {
      return MMAPIO_EILSEQ
        /* since beyond U+10FFFF, no valid UTF-16 encoding */;
    }
  }
  (*outlen) = n;
  return 0;
}

wchar_t* mmapio_u8towc(unsigned char const* nm) {
  /* use in-house wide character conversion */
  size_t ns;
  wchar_t* out;
  /* try the length */{
    int err = mmapio_u8towc_shim(nm, NULL, &ns);
    if (err != 0) {
      /* conversion error caused by bad sequence, so */return NULL;
    }
  }
  out = (wchar_t*)calloc(ns+1, sizeof(wchar_t));
  if (out != NULL) {
    mmapio_u8towc_shim(nm, out, &ns);
    out[ns] = 0;
  }
  return out;
}

size_t mmapio_file_size_e(HANDLE fd) {
  LARGE_INTEGER sz;
  BOOL res = GetFileSizeEx(fd, &sz);
  if (res) {
#if (defined ULLONG_MAX)
    return (size_t)sz.QuadPart;
#else
    return ((size_t)sz.u.LowPart)
      |    (((size_t)sz.u.HighPart)<<32);
#endif /*ULLONG_MAX*/
  } else return 0u;
}

DWORD mmapio_mode_prot_cvt(int mmode) {
  switch (mmode) {
  case mmapio_mode_write:
    return PAGE_READWRITE;
  case mmapio_mode_read:
    return PAGE_READONLY;
  default:
    return 0;
  }
}

DWORD mmapio_mode_access_cvt(struct mmapio_mode_tag const mt) {
  DWORD flags = 0;
  switch (mt.mode) {
  case mmapio_mode_write:
    flags = FILE_MAP_READ|FILE_MAP_WRITE;
    break;
  case mmapio_mode_read:
    flags = FILE_MAP_READ;
    break;
  default:
    return 0;
  }
  if (mt.privy) {
    flags |= FILE_MAP_COPY;
  }
  return flags;
}

struct mmapio_i* mmapio_open_rest
  (HANDLE fd, struct mmapio_mode_tag const mt, size_t sz, size_t off)
{
  /*
   * based on
   * https://docs.microsoft.com/en-us/windows/win32/memory/
   *   creating-a-view-within-a-file
   */
  struct mmapio_win32 *const out = calloc(1, sizeof(struct mmapio_win32));
  void *ptr;
  size_t fullsize;
  size_t fullshift;
  size_t fulloff;
  size_t extended_size;
  size_t const size_clamp = mmapio_file_size_e(fd);
  HANDLE fmd;
  SECURITY_ATTRIBUTES cfmsa;
  if (out == NULL) {
    CloseHandle(fd);
    return NULL;
  }
  if (mt.end) /* fix map size */{
    size_t const xsz = size_clamp;
    if (xsz < off) {
      /* reject non-ending zero parameter */
      CloseHandle(fd);
      free(out);
      return NULL;
    } else sz = xsz-off;
  } else if (sz == 0) {
    /* reject non-ending zero parameter */
    CloseHandle(fd);
    free(out);
    return NULL;
  }
  /* fix to allocation granularity */{
    DWORD psize;
    /* get the allocation granularity */{
      SYSTEM_INFO s_info;
      GetSystemInfo(&s_info);
      psize = s_info.dwAllocationGranularity;
    }
    fullsize = sz;
    if (psize > 0) {
      /* adjust the offset */
      fullshift = off%psize;
      fulloff = (off-fullshift);
      if (fullshift >= ((~(size_t)0u)-sz)) {
        /* range fix failure */
        CloseHandle(fd);
        free(out);
        errno = ERANGE;
        return NULL;
      } else fullsize += fullshift;
      /* adjust the size */{
        size_t size_shift = (fullsize % psize);
        if (size_shift > 0) {
          extended_size = fullsize + (psize - size_shift);
        } else extended_size = fullsize;
      }
    } else {
      fullshift = 0u;
      fulloff = off;
      extended_size = sz;
    }
  }
  /* prepare the security attributes */{
    memset(&cfmsa, 0, sizeof(cfmsa));
    cfmsa.nLength = sizeof(cfmsa);
    cfmsa.lpSecurityDescriptor = NULL;
    cfmsa.bInheritHandle = (BOOL)(mt.bequeath ? TRUE : FALSE);
  }
  /* create the file mapping object */{
    /*
     * clamp size to end of file;
     * based on https://stackoverflow.com/a/46014637
     */
    size_t const fullextent = size_clamp > extended_size+fulloff
        ? extended_size + fulloff
        : size_clamp;
    fmd = CreateFileMappingA(
        fd, /*hFile*/
        &cfmsa, /*lpFileMappingAttributes*/
        mmapio_mode_prot_cvt(mt.mode), /*flProtect*/
        (DWORD)((fullextent>>32)&0xFFffFFff), /*dwMaximumSizeHigh*/
        (DWORD)(fullextent&0xFFffFFff), /*dwMaximumSizeLow*/
        NULL /*lpName*/
      );
  }
  if (fmd == NULL) {
    /* file mapping failed */
    CloseHandle(fd);
    free(out);
    return NULL;
  }
  ptr = MapViewOfFile(
      fmd, /*hFileMappingObject*/
      mmapio_mode_access_cvt(mt), /*dwDesiredAccess*/
      (DWORD)((fulloff>>32)&0xFFffFFff), /* dwFileOffsetHigh */
      (DWORD)(fulloff&0xFFffFFff), /* dwFileOffsetLow */
      (SIZE_T)(fullsize) /* dwNumberOfBytesToMap */
    );
  if (ptr == NULL) {
    CloseHandle(fmd);
    CloseHandle(fd);
    free(out);
    return NULL;
  }
  /* initialize the interface */{
    out->ptr = ptr;
    out->len = fullsize;
    out->fd = fd;
    out->fmd = fmd;
    out->shift = fullshift;
    out->base.mmi_dtor = &mmapio_mmi_dtor;
    out->base.mmi_acquire = &mmapio_mmi_acquire;
    out->base.mmi_release = &mmapio_mmi_release;
    out->base.mmi_length = &mmapio_mmi_length;
  }
  return (struct mmapio_i*)out;
}

void mmapio_mmi_dtor(struct mmapio_i* m) {
  struct mmapio_win32* const mu = (struct mmapio_win32*)m;
  UnmapViewOfFile(mu->ptr);
  mu->ptr = NULL;
  CloseHandle(mu->fmd);
  mu->fmd = NULL;
  CloseHandle(mu->fd);
  mu->fd = NULL;
  free(mu);
  return;
}

void* mmapio_mmi_acquire(struct mmapio_i* m) {
  struct mmapio_win32* const mu = (struct mmapio_win32*)m;
  return ((unsigned char*)mu->ptr)+mu->shift;
}

void mmapio_mmi_release(struct mmapio_i* m, void* p) {
  return;
}

size_t mmapio_mmi_length(struct mmapio_i const* m) {
  struct mmapio_win32* const mu = (struct mmapio_win32*)m;
  return mu->len-mu->shift;
}
#endif /*MMAPIO_OS*/
/* END   static functions */

/* BEGIN configuration functions */
int mmapio_get_os(void) {
  return (int)(MMAPIO_OS);
}

int mmapio_check_bequeath_stop(void) {
#if MMAPIO_OS == MMAPIO_OS_UNIX
#  if (defined O_CLOEXEC)
  return 1;
#  else
  return 0;
#  endif /*O_CLOEXEC*/
#elif MMAPIO_OS == MMAPIO_OS_WIN32
  return 1;
#else
  return -1;
#endif /*MMAPIO_OS*/
}
/* END   configuration functions */

/* BEGIN helper functions */
void mmapio_close(struct mmapio_i* m) {
  (*m).mmi_dtor(m);
  return;
}

void* mmapio_acquire(struct mmapio_i* m) {
  return (*m).mmi_acquire(m);
}

void mmapio_release(struct mmapio_i* m, void* p) {
  (*m).mmi_release(m,p);
  return;
}

size_t mmapio_length(struct mmapio_i const* m) {
  return (*m).mmi_length(m);
}
/* END   helper functions */

/* BEGIN open functions */
#if MMAPIO_OS == MMAPIO_OS_UNIX
struct mmapio_i* mmapio_open
  (char const* nm, char const* mode, size_t sz, size_t off)
{
  int fd;
  struct mmapio_mode_tag const mt = mmapio_mode_parse(mode);
  fd = open(nm, mmapio_mode_rw_cvt(mt.mode));
  if (fd == -1) {
    /* can't open file, so */return NULL;
  }
  return mmapio_open_rest(fd, mt, sz, off);
}

struct mmapio_i* mmapio_u8open
  (unsigned char const* nm, char const* mode, size_t sz, size_t off)
{
  int fd;
  struct mmapio_mode_tag const mt = mmapio_mode_parse(mode);
  fd = open((char const*)nm, mmapio_mode_rw_cvt(mt.mode));
  if (fd == -1) {
    /* can't open file, so */return NULL;
  }
  return mmapio_open_rest(fd, mt, sz, off);
}

struct mmapio_i* mmapio_wopen
  (wchar_t const* nm, char const* mode, size_t sz, size_t off)
{
  int fd;
  struct mmapio_mode_tag const mt = mmapio_mode_parse(mode);
  char* const mbfn = mmapio_wctomb(nm);
  if (mbfn == NULL) {
    /* conversion failure, so give up */
    free(mbfn);
    return NULL;
  }
  fd = open(mbfn, mmapio_mode_rw_cvt(mt.mode));
  free(mbfn);
  if (fd == -1) {
    /* can't open file, so */return NULL;
  }
  return mmapio_open_rest(fd, mt, sz, off);
}
#elif MMAPIO_OS == MMAPIO_OS_WIN32
struct mmapio_i* mmapio_open
  (char const* nm, char const* mode, size_t sz, size_t off)
{
  HANDLE fd;
  struct mmapio_mode_tag const mt = mmapio_mode_parse(mode);
  SECURITY_ATTRIBUTES cfsa;
  memset(&cfsa, 0, sizeof(cfsa));
  cfsa.nLength = sizeof(cfsa);
  cfsa.lpSecurityDescriptor = NULL;
  cfsa.bInheritHandle = (BOOL)(mt.bequeath ? TRUE : FALSE);
  fd = CreateFileA(
      nm, mmapio_mode_rw_cvt(mt.mode),
      FILE_SHARE_READ|FILE_SHARE_WRITE,
      &cfsa,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      NULL
    );
  if (fd == INVALID_HANDLE_VALUE) {
    /* can't open file, so */return NULL;
  }
  return mmapio_open_rest(fd, mt, sz, off);
}

struct mmapio_i* mmapio_u8open
  (unsigned char const* nm, char const* mode, size_t sz, size_t off)
{
  HANDLE fd;
  struct mmapio_mode_tag const mt = mmapio_mode_parse(mode);
  wchar_t* const wcfn = mmapio_u8towc(nm);
  SECURITY_ATTRIBUTES cfsa;
  memset(&cfsa, 0, sizeof(cfsa));
  cfsa.nLength = sizeof(cfsa);
  cfsa.lpSecurityDescriptor = NULL;
  cfsa.bInheritHandle = (BOOL)(mt.bequeath ? TRUE : FALSE);
  if (wcfn == NULL) {
    /* conversion failure, so give up */
    return NULL;
  }
  fd = CreateFileW(
      wcfn, mmapio_mode_rw_cvt(mt.mode),
      FILE_SHARE_READ|FILE_SHARE_WRITE,
      &cfsa,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      NULL
    );
  free(wcfn);
  if (fd == INVALID_HANDLE_VALUE) {
    /* can't open file, so */return NULL;
  }
  return mmapio_open_rest(fd, mt, sz, off);
}

struct mmapio_i* mmapio_wopen
  (wchar_t const* nm, char const* mode, size_t sz, size_t off)
{
  HANDLE fd;
  struct mmapio_mode_tag const mt = mmapio_mode_parse(mode);
  SECURITY_ATTRIBUTES cfsa;
  memset(&cfsa, 0, sizeof(cfsa));
  cfsa.nLength = sizeof(cfsa);
  cfsa.lpSecurityDescriptor = NULL;
  cfsa.bInheritHandle = (BOOL)(mt.bequeath ? TRUE : FALSE);
  fd = CreateFileW(
      nm, mmapio_mode_rw_cvt(mt.mode),
      FILE_SHARE_READ|FILE_SHARE_WRITE,
      &cfsa,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      NULL
    );
  if (fd == INVALID_HANDLE_VALUE) {
    /* can't open file, so */return NULL;
  }
  return mmapio_open_rest(fd, mt, sz, off);
}
#else
struct mmapio_i* mmapio_open
  (char const* nm, char const* mode, size_t sz, size_t off)
{
  /* no-op */
  return NULL;
}

struct mmapio_i* mmapio_u8open
  (unsigned char const* nm, char const* mode, size_t sz, size_t off)
{
  /* no-op */
  return NULL;
}

struct mmapio_i* mmapio_wopen
  (wchar_t const* nm, char const* mode, size_t sz, size_t off)
{
  /* no-op */
  return NULL;
}
#endif /*MMAPIO_OS*/
/* END   open functions */

