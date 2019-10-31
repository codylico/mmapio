/*
 * \file mmapio.h
 * \brief Memory-mapped files
 * \author Cody Licorish (svgmovement@gmail.com)
 */
#ifndef hg_MMapIO_mmapIo_H_
#define hg_MMapIO_mmapIo_H_

#include <stddef.h>

#ifdef MMAPIO_WIN32_DLL
#  ifdef MMAPIO_WIN32_DLL_INTERNAL
#    define MMAPIO_API __declspec(dllexport)
#  else
#    define MMAPIO_API __declspec(dllimport)
#  endif /*MMAPIO_DLL_INTERNAL*/
#else
#  define MMAPIO_API
#endif /*MMAPIO_WIN32_DLL*/

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/**
 * \brief Operating system identifier.
 */
enum mmapio_os {
  mmapio_os_none = 0,
  mmapio_os_unix = 1
};

/**
 * \brief File memory access modes.
 */
enum mmapio_mode {
  mmapio_mode_read = 0x72,
  mmapio_mode_write = 0x77,
  mmapio_mode_end = 0x65,
  mmapio_mode_private = 0x70
};

/**
 * \brief Memory-mapped input-output interface.
 */
struct mmapio_i {
  /**
   * \brief Destructor; closes the file and frees the space.
   * \param m map instance
   */
  void (*mmi_dtor)(struct mmapio_i* m);
  /**
   * \brief Acquire a lock to the space.
   * \param m map instance
   * \return pointer to locked space on success, NULL otherwise
   */
  void* (*mmi_acquire)(struct mmapio_i* m);
  /**
   * \brief Release a lock of the space.
   * \param m map instance
   * \param p pointer of region to release
   */
  void (*mmi_release)(struct mmapio_i* m, void* p);
  /**
   * \brief Check the length of the mapped area.
   * \param m map instance
   * \return the length of the mapped region exposed by this interface
   */
  size_t (*mmi_length)(struct mmapio_i const* m);
};

/* BEGIN configurations */
/**
 * \brief Check the library's target backend.
 * \return a \link mmapio_os \endlink value
 */
MMAPIO_API
int mmapio_get_os(void);
/* END   configurations */

/* BEGIN helper functions */
/**
 * \brief Helper function closes the file.
 * \param m map instance
 */
MMAPIO_API
void mmapio_close(struct mmapio_i* m);

/**
 * \brief Helper function acquires file data.
 * \param m map instance
 * \return pointer to locked space on success, NULL otherwise
 */
MMAPIO_API
void* mmapio_acquire(struct mmapio_i* m);

/**
 * \brief Helper function to release a lock of the space.
 * \param m map instance
 * \param p pointer of region to release
 */
MMAPIO_API
void mmapio_release(struct mmapio_i* m, void* p);

/**
 * \brief Helper function to check the length of the space.
 * \param m map instance
 * \return the length of the space
 */
MMAPIO_API
size_t mmapio_length(struct mmapio_i const* m);
/* END   helper functions */

/* BEGIN open functions */
/**
 * \brief Open a file using a narrow character name.
 * \param nm name of file to map
 * \param mode one of 'r' (for readonly) or 'w' (writeable),
 *   optionally followed by 'e' to extend map to end of file,
 *   optionally followed by 'p' to make write changes private
 * \param sz size in bytes of region to map
 * \param off file offset of region to map
 * \return an interface on success, NULL otherwise
 * \note On Windows, this function uses `CreateFileA` directly.
 * \note On Unix, this function uses the `open` system call directly.
 */
struct mmapio_i* mmapio_open
  (char const* nm, char const* mode, size_t sz, size_t off);

/**
 * \brief Open a file using a UTF-8 encoded name.
 * \param nm name of file to map
 * \brief mode one of 'r' (for readonly) or 'w' (writeable),
 *   optionally followed by 'e' to extend map to end of file,
 *   optionally followed by 'p' to make write changes private
 * \param sz size in bytes of region to map
 * \param off file offset of region to map
 * \return an interface on success, NULL otherwise
 * \note On Windows, this function re-encodes the `nm` parameter from
 *   UTF-8 to UTF-16, then uses `CreateFileW` on the result.
 * \note On Unix, this function uses the `open` system call directly.
 */
struct mmapio_i* mmapio_u8open
  (unsigned char const* nm, char const* mode, size_t sz, size_t off);

/**
 * \brief Open a file using a wide character name.
 * \param nm name of file to map
 * \brief mode one of 'r' (for readonly) or 'w' (writeable),
 *   optionally followed by 'e' to extend map to end of file,
 *   optionally followed by 'p' to make write changes private
 * \param sz size in bytes of region to map
 * \param off file offset of region to map
 * \return an interface on success, NULL otherwise
 * \note On Windows, this function uses `CreateFileW` directly.
 * \note On Unix, this function translates the wide string
 *   to a multibyte character string, then passes the result to
 *   the `open` system call. Use `setlocale` in advance if necessary.
 */
struct mmapio_i* mmapio_wopen
  (wchar_t const* nm, char const* mode, size_t sz, size_t off);
/* END   oepn functions */

#ifdef __cplusplus
};
#endif /*__cplusplus*/

#endif /*hg_MMapIO_mmapIo_H_*/

