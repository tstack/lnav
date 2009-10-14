
#ifndef __termios_guard_hh
#define __termios_guard_hh

class guard_termios {
public:
    guard_termios(int fd) : gt_fd(fd) {
	
	if (isatty(this->gt_fd) &&
	    tcgetattr(this->gt_fd, &this->gt_termios) == -1) {
	    perror("tcgetattr");
	}

    };
    
    ~guard_termios() {
	if (isatty(this->gt_fd) &&
	    tcsetattr(this->gt_fd, TCSANOW, &this->gt_termios) == -1) {
	    perror("tcsetattr");
	}
    };

private:
    int gt_fd;
    struct termios gt_termios;
};

#endif
