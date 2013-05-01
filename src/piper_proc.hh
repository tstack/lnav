/**
 * @file piper_proc.hh
 */

#ifndef __piper_proc_hh
#define __piper_proc_hh

#include <string>
#include <sys/types.h>

/**
 * Creates a subprocess that reads data from a pipe and writes it to a file so
 * lnav can treat it like any other file and do preads.
 *
 * TODO: Add support for gzipped files.
 */
class piper_proc {
public:
    class error
	: public std::exception {
public:
	error(int err)
	    : e_err(err) { };

	int e_err;
    };

    /**
     * Forks a subprocess that will read data from the given file descriptor
     * and write it to a temporary file.
     *
     * @param pipefd The file descriptor to read the file contents from.
     * @param timestamp True if an ISO 8601 timestamp should be prepended onto
     *   the lines read from pipefd.
     * @param filename The name of the file to save the input to, otherwise a
     *   temporary file will be created.
     */
    piper_proc(int pipefd, bool timestamp, const char *filename=NULL);

    /**
     * Terminates the child process.
     */
    virtual ~piper_proc();

    /** @return The file descriptor for the temporary file. */
    int get_fd() const { return this->pp_fd; };
    
private:
    /** A file descriptor that refers to the temporary file. */
    int pp_fd;

    /** Indicates whether timestamps should be added to the input. */
    bool pp_timestamp;

    /** The child process' pid. */
    pid_t pp_child;
};

#endif
