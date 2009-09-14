
#ifndef __piper_proc_hh
#define __piper_proc_hh

#include <string>
#include <sys/types.h>

class piper_proc {
public:
    class error
	: public std::exception {
public:
	error(int err)
	    : e_err(err) { };

	int e_err;
    };
    
    piper_proc(int pipefd);
    virtual ~piper_proc();

    int get_fd() const { return this->pp_fd; };
    
private:
    std::string pp_filename;
    int pp_fd;
    pid_t pp_child;
};

#endif
