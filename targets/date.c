#include <assert.h>

static int is_leap(int year) {
  if (year % 400 == 0) {
    return 1;
  }
  if (year % 100 == 0) {
    return 0;
  }
  if (year % 4 == 0) {
    return 1;
  }
  return 0;
}

static int days_in_month(int year, int month) {
  if (month == 2) {
    return is_leap(year) ? 29 : 28;
  }
  if (month == 4 || month == 6 || month == 9 || month == 11) {
    return 30;
  }
  return 31;
}

int check(int year, int month, int day) {
  if (year < 1900 || year > 2100) {
    return -1;
  }
  if (month < 1 || month > 12) {
    return -2;
  }
  if (day < 1) {
    return -3;
  }
  if (day > days_in_month(year, month)) {
    return -4;
  }
  assert(day <= 31);
  if (month == 2 && day == 29) {
    assert(is_leap(year));
    return 1;
  }
  return 0;
}
