/**
 * @file termios_guard.hh
 */

#ifndef __termios_guard_hh
#define __termios_guard_hh

/**
 * RAII class that saves the current termios for a tty and then restores them
 * during destruction.
 */
class guard_termios {
public:
    /**
     * Store the TTY termios settings in this object.
     *
     * @param fd The tty file descriptor.
     */
    guard_termios(const int fd) : gt_fd(fd) {
	if (isatty(this->gt_fd) &&
	    tcgetattr(this->gt_fd, &this->gt_termios) == -1) {
	    perror("tcgetattr");
	}

    };

    /**
     * Restore the TTY termios settings that were captured when this object was
     * instantiated.
     */
    ~guard_termios() {
	if (isatty(this->gt_fd) &&
	    tcsetattr(this->gt_fd, TCSANOW, &this->gt_termios) == -1) {
	    perror("tcsetattr");
	}
    };

private:
    const int gt_fd;
    struct termios gt_termios;
};

#endif
