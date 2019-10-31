
#include "../mmapio.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <ctype.h>

int main(int argc, char **argv) {
  struct mmapio_i* mi;
  char const* fname;
  if (argc < 5) {
    fputs("usage: dump (file) (mode) (offset) (length)\n", stderr);
    return EXIT_FAILURE;
  }
  fname = argv[1];
  mi = mmapio_open(fname, argv[2],
    (size_t)strtoul(argv[3],NULL,0),
    (size_t)strtoul(argv[4],NULL,0));
  if (mi == NULL) {
    fprintf(stderr, "failed to map file '%s'\n", fname);
    return EXIT_FAILURE;
  } else {
    /* output the data */{
      size_t len = mmapio_length(mi);
      unsigned char* bytes = (unsigned char*)mmapio_acquire(mi);
      if (bytes != NULL) {
        size_t i;
        if (len >= UINT_MAX-32)
          len = UINT_MAX-32;
        for (i = 0; i < len; i+=16) {
          size_t j = 0;
          fprintf(stdout, "%s%4lx:", i?"\n":"", (long unsigned int)i);
          for (j = 0; j < 16; ++j) {
            if (j%4 == 0) {
              fputs(" ", stdout);
            }
            if (j < len-i)
              fprintf(stdout, "%02x", (unsigned int)(bytes[i+j]));
            else fputs("  ", stdout);
          }
          fputs(" | ", stdout);
          for (j = 0; j < 16; ++j) {
            if (j < len-i) {
              int ch = bytes[i+j];
              fprintf(stdout, "%c", isprint(ch) ? ch : '.');
            } else fputs(" ", stdout);
          }
        }
        fputs("\n", stdout);
        mmapio_release(mi, bytes);
      } else {
        fprintf(stderr, "mapped file '%s' gives no bytes?\n", fname);
      }
    }
    mmapio_close(mi);
  }
  return EXIT_SUCCESS;
}

