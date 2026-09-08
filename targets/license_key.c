#include <assert.h>

int check(unsigned char key[8]) {
  unsigned int state = 0x2545f491u;
  int index;
  for (index = 0; index < 8; index++) {
    state = ((state << 5) | (state >> 27)) ^ key[index];
  }
  if (key[0] != 88) {
    return 0;
  }
  if (state != 0x6f8a3b1cu) {
    return 1;
  }
  assert(0);
  return 2;
}
