/*
 * \file mmapio.c
 * \brief Memory-mapped files
 * \author Cody Licorish (svgmovement@gmail.com)
 */
#define MMAPIO_WIN32_DLL_INTERNAL
#define _POSIX_C_SOURCE 200112L
#include "mmapio.h"
#include <stdlib.h>

#ifndef MMAPIO_MAX_CACHE
#  define MMAPIO_MAX_CACHE 1048576
#endif /*MMAPIO_MAX_CACHE*/

struct mmapio_mode_tag {
  char mode;
  char end;
  char privy;
};

/**
 * \brief Extract a mmapio mode tag from a mode text.
 * \param mmode the value to parse
 * \return a mmapio mode tag
 */
static struct mmapio_mode_tag mmapio_mode_parse(char const* mmode);

#define MMAPIO_OS_UNIX 1

/*
 * inspired by https://stackoverflow.com/a/30971057
 * and https://stackoverflow.com/a/11351171
 */
#ifndef MMAPIO_OS
#  if (defined __unix__) || (defined(__APPLE__)&&defined(__MACH__))
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

struct mmapio_unix {
  struct mmapio_i base;
  void* ptr;
  size_t len;
  size_t shift;
  int fd;
};

/**
 * \brief Convert a wide string to a multibyte string.
 * \param nm the string to convert
 * \return a multibyte string on success, NULL otherwise
 */
static char* mmapio_wctomb(wchar_t const* nm);

/**
 * \brief Convert a mmapio mode text to a POSIX `open` flag.
 * \param mmode the value to convert
 * \return an `open` flag on success, zero otherwise
 */
static int mmapio_mode_rw_cvt(int mmode);

/**
 * \brief Convert a mmapio mode text to a POSIX `mmap` protection flag.
 * \param mmode the value to convert
 * \return an `mmap` protection flag on success, zero otherwise
 */
static int mmapio_mode_prot_cvt(int mmode);

/**
 * \brief Convert a mmapio mode text to a POSIX `mmap` others' flag.
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
 * \param mmode mode text
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
 * \param sz size of output buffer
 * \param off offset from start of mapped region
 * \return pointer to locked space on success, NULL otherwise
 */
static void* mmapio_mmi_acquire(struct mmapio_i* m, size_t sz, size_t off);

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
  struct mmapio_mode_tag out = { 0, 0, 0 };
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
  switch (mmode) {
  case mmapio_mode_write:
    return O_RDWR;
  case mmapio_mode_read:
    return O_RDONLY;
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
    } else fulloff = (off_t)off;
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

void* mmapio_mmi_acquire(struct mmapio_i* m, size_t sz, size_t off) {
  struct mmapio_unix* const mu = (struct mmapio_unix*)m;
  if (off >= mu->len
  ||  sz > mu->len-off)
    /* failed range check, so */return NULL;
  else return mu->ptr+off+mu->shift;
}

void mmapio_mmi_release(struct mmapio_i* m, void* p) {
  return;
}

size_t mmapio_mmi_length(struct mmapio_i const* m) {
  struct mmapio_unix* const mu = (struct mmapio_unix*)m;
  return mu->len-mu->shift;
}
#endif /*MMAPIO_OS*/
/* END   static functions */

/* BEGIN configuration functions */
int mmapio_get_os(void) {
  return (int)(MMAPIO_OS);
}
/* END   configuration functions */

/* BEGIN helper functions */
void mmapio_close(struct mmapio_i* m) {
  (*m).mmi_dtor(m);
  return;
}

void* mmapio_acquire(struct mmapio_i* m, size_t sz, size_t off) {
  return (*m).mmi_acquire(m,sz,off);
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
#else
struct mmapio_i* mmapio_open(char const* nm, char const* mode) {
  /* no-op */
  return NULL;
}

struct mmapio_i* mmapio_u8open(unsigned char const* nm, char const* mode) {
  /* no-op */
  return NULL;
}

struct mmapio_i* mmapio_wopen(wchar_t const* nm, char const* mode) {
  /* no-op */
  return NULL;
}
#endif /*MMAPIO_ON_UNIX*/
/* END   open functions */

