#include <assert.h>

int check(int table[8], int key) {
  int index;
  for (index = 1; index < 8; index++) {
    if (table[index - 1] > table[index]) {
      return -2;
    }
  }
  int low = 0;
  int high = 7;
  int found = -1;
  while (low <= high) {
    int middle = low + (high - low) / 2;
    if (table[middle] == key) {
      found = middle;
      break;
    }
    if (table[middle] < key) {
      low = middle + 1;
    } else {
      high = middle - 1;
    }
  }
  if (found >= 0) {
    assert(table[found] == key);
  }
  return found;
}
