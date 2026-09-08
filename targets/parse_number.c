#include <assert.h>

int check(char text[8]) {
  int index = 0;
  int sign = 1;
  long value = 0;
  int digits = 0;
  if (text[0] == '-') {
    sign = -1;
    index = 1;
  }
  while (index < 8) {
    char c = text[index];
    if (c < '0' || c > '9') {
      break;
    }
    value = value * 10 + (c - '0');
    if (value > 2147483647L) {
      return -1;
    }
    digits++;
    index++;
  }
  if (digits == 0) {
    return -2;
  }
  assert(value >= 0);
  return (int)(sign * value);
}
