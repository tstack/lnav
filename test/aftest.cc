
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "auto_fd.hh"

void foo(int *fd)
{
  *fd = 2;
}

int main(int argc, char *argv[])
{
  {
    auto_fd fd(open("/dev/null", O_WRONLY));
    auto_fd fd2;
    
    printf("1 fd %d\n", fd.get());
    fd = -1;
    printf("2 fd %d\n", fd.get());

    fd = open("/dev/null", O_WRONLY);
    fd2 = fd;
    printf("3 fd %d\n", fd.get());
    printf("4 fd2 %d\n", fd2.get());

    foo(fd2.out());
    printf("5 fd2 %d\n", fd2.get());
  }

  printf("nfd %d\n", open("/dev/null", O_WRONLY));
}
