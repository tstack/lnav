#include "timer.hh"
#include "lnav_log.hh"

const struct itimerval disable = {
    { 0, 0 },
    { 0, 0 }
};

timer::error::error(int err):e_err(err) { }

timer::interrupt_timer::interrupt_timer(struct timeval t,
        sighandler_t_ sighandler=SIG_IGN) : new_handler(sighandler),
        old_handler(NULL), new_val((struct itimerval){{0,0},t}),
        old_val(disable), armed(true) { }

int timer::interrupt_timer::arm_timer() {
    // Disable the interval timer before setting the handler and arming the
    // interval timer or else we will have a race-condition where the timer
    // might fire and the appropriate handler might not be set.
    if (setitimer(ITIMER_REAL, &disable, &this->old_val) != 0) {
        log_error("Unable to disable the timer: %s",
                  strerror(errno));
        return -1;
    }
    this->old_handler = signal(SIGALRM, this->new_handler);
    if (this->old_handler == SIG_ERR) {
        log_error("Unable to set the signal handler: %s",
                  strerror(errno));
        if (setitimer(ITIMER_REAL, &this->old_val, NULL) != 0) {
            log_error("Unable to reset the interrupt timer: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
        return -1;
    }

    if (setitimer(ITIMER_REAL, &this->new_val, NULL) != 0) {
        if(signal(SIGALRM, this->old_handler) == SIG_ERR) {
            log_error("Unable to reset the signal handler: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
        log_error("Unable to set the timer: %s", strerror(errno));
        return -1;
    }
    this->armed = true;
    return 0;
}

timer::interrupt_timer::~interrupt_timer() {
    if (this->armed) {
        // Disable the interval timer before resetting the handler and rearming
        // the previous interval timer or else we will have a race-condition
        // where the timer might fire and the appropriate handler might not be
        // set.
        if (setitimer(ITIMER_REAL, &disable, NULL) != 0) {
            log_error("Failed to disable the timer: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
        if (signal(SIGALRM, this->old_handler) == SIG_ERR) {
            log_error("Failed to reinstall previous SIGALRM handler: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
        if (setitimer(ITIMER_REAL, &this->old_val, NULL) != 0) {
            log_error("Failed to reset the timer to previous value: %s",
                      strerror(errno));
            throw timer::error(errno);
        }
    }
}
