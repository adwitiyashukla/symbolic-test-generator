#include <assert.h>

int check(int index, int divisor, int values[4]) {
  int table[4];
  int i;
  for (i = 0; i < 4; i++) {
    table[i] = values[i];
  }
  if (index > 2) {
    table[index] = 7;
  }
  int total = 0;
  for (i = 0; i < 4; i++) {
    total = total + table[i];
  }
  if (divisor < 10) {
    total = total / divisor;
  }
  assert(total != 42);
  return total;
}
