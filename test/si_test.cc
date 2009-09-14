
#include <stdio.h>
#include "strong_int.hh"

class __dsi1_distinct;
typedef strong_int<int, __dsi1_distinct> dsi1_t;
class __dsi2_distinct;
typedef strong_int<int, __dsi2_distinct> dsi2_t;

STRONG_INT_TYPE(int, dsi3);

int main(int argc, char *argv[])
{
  dsi1_t dsi1(0);
  dsi2_t dsi2(1);
  dsi3_t dsi3(2);

  printf("%d\n", sizeof(dsi1));
}
