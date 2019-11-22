
#include "../mmapio.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  printf("check bequeath stop: %s\n",
    mmapio_check_bequeath_stop()?"true":"false");
  return EXIT_SUCCESS;
}

