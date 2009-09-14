
#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>

#include "auto_fd.hh"

int main(int argc, char *argv[])
{
    int retval = EXIT_SUCCESS;
    auto_fd fd1, fd2;
    int tmp;
    
    assert(fd1 == -1);
    tmp = open("/dev/null", O_RDONLY);
    assert(tmp != -1);
    fd1 = tmp;
    fd1 = tmp;
    assert(fcntl(tmp, F_GETFL) >= 0);
    fd1 = fd2;
    assert(fcntl(tmp, F_GETFL) == -1);
    assert(errno == EBADF);
    assert(fd1 == -1);

    tmp = open("/dev/null", O_RDONLY);
    assert(tmp != -1);
    fd1 = tmp;
    *fd1.out() = STDOUT_FILENO;
    assert(fcntl(tmp, F_GETFL) == -1);
    assert(errno == EBADF);

    {
	auto_fd fd_cp(const_cast<const auto_fd &>(fd1));

	assert(fd1 == STDOUT_FILENO);
	assert(fd_cp != STDOUT_FILENO);
	assert(fd_cp != -1);

	tmp = (int)fd_cp;
    }
    {
	auto_fd fd_cp(const_cast<const auto_fd &>(fd1));

	assert(fd_cp == tmp);
    }
    assert(fd1.release() == STDOUT_FILENO);
    assert(fd1 == -1);
    
    return retval;
}
