#ifndef timer_hh
#define timer_hh

#include <errno.h>
#include <signal.h>
#include <exception>
#include <sys/time.h>
#include <sys/types.h>

// Linux and BSD seem to name the function pointer to signal handler differently.
// Linux names it 'sighandler_t' and BSD names it 'sig_t'. Instead of depending
// on configure scripts to find out the type name, typedef it right here.
typedef void (*sighandler_t_)(int);

namespace timer{
class error : public std::exception {
    public:
        error(int err);
        int e_err;
};

class interrupt_timer {
    public:
        interrupt_timer(struct timeval, sighandler_t_);
        int arm_timer();
        ~interrupt_timer();
    private:
        sighandler_t_ new_handler, old_handler;
        struct itimerval new_val, old_val;
        bool armed;
};
}
#endif
