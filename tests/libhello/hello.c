#include "hello.h"

void hello(const char *string) {
  if(string) puts(string);
  else puts("Hello, world!");
}
