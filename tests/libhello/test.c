#include <hello.h>

int main(int argc, char *argv[]) {
  if(argc == 1) hello(NULL);
  else hello(argv[1]);
  return 0;
}
