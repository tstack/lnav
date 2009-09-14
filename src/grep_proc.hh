/**
 * @file grep_proc.hh
 */

#ifndef __grep_proc_hh
#define __grep_proc_hh

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#ifdef HAVE_PCRE_H
#include <pcre.h>
#elif HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#endif

#include <deque>
#include <string>
#include <vector>
#include <exception>

#include "pcrepp.hh"
#include "auto_fd.hh"
#include "auto_mem.hh"
#include "strong_int.hh"
#include "line_buffer.hh"

/** Strongly-typed integer for matched line numbers. */
STRONG_INT_TYPE(int, grep_line);

class grep_proc;

/**
 * Data source for lines to be searched using a grep_proc.
 */
class grep_proc_source {
public:
    virtual ~grep_proc_source() { };

    /**
     * Get the value for a particular line in the source.
     *
     * @param line The line to retrieve.
     * @param value_out The destination for the line value.
     */
    virtual bool grep_value_for_line(int line, std::string &value_out) = 0;

    virtual std::string grep_source_name(void) { return ""; };
};

class grep_proc_control {
public:

    virtual ~grep_proc_control() { };

    /** @param msg The error encountered while attempting the grep. */
    virtual void grep_error(std::string msg) { };
};

/**
 * Sink for matches produced by a grep_proc instance.
 */
class grep_proc_sink {
public:
    virtual ~grep_proc_sink() { };

    /** Called at the start of a new grep run. */
    virtual void grep_begin(grep_proc &gp) { };

    /** Called periodically between grep_begin and grep_end. */
    virtual void grep_end_batch(grep_proc &gp) { };

    /** Called at the end of a grep run. */
    virtual void grep_end(grep_proc &gp) { };

    /**
     * Called when a match is found on 'line' and between [start, end).
     *
     * @param line The line number that matched.
     * @param start The offset within the line where the match begins.
     * @param end The offset of the character after the last character in the
     * match.
     */
    virtual void grep_match(grep_proc &gp,
			    grep_line_t line,
			    int start,
			    int end) = 0;

    /**
     * Called for each captured substring in the line.
     *
     * @param line The line number that matched.
     * @param start The offset within the line where the capture begins.
     * @param end The offset of the character after the last character in the
     * capture.
     * @param capture The captured substring itself.
     */
    virtual void grep_capture(grep_proc &gp,
			      grep_line_t line,
			      int start,
			      int end,
			      char *capture) { };
};

/**
 * "Grep" that runs in a separate process so it doesn't stall user-interaction.
 * This class manages the child process and any interactions between the parent
 * and child.  The source data to be matched comes from the grep_proc_source
 * delegate and the results are sent to the grep_proc_sink delegate in the
 * parent process.
 *
 * Note: The "grep" executable is not actually used, instead we use the pcre(3)
 * library directly.
 */
class grep_proc {
public:
    class error
	: public std::exception {
public:
	error(int err)
	    : e_err(err) { };

	int e_err;
    };

    /**
     * Construct a grep_proc object.  This involves compiling the regular
     * expression and then forking off the child process.  Note that both the
     * parent and child return from this call and you must call the start()
     * method immediately afterward to get things going.
     *
     * @param code The pcre code to run over the lines of input.
     * @param gps The source of the data to match.
     * @param readfds The file descriptor set for readable fds.
     */
    grep_proc(pcre *code,
	      grep_proc_source &gps,
	      int &maxfd,
	      fd_set &readfds);

    virtual ~grep_proc();

    /** @return The code passed in to the constructor. */
    pcre *get_code() { return this->gp_code; };

    /** @param gpd The sink to send resuls to. */
    void set_sink(grep_proc_sink *gpd)
    {
	this->gp_sink = gpd;
	if (gpd != NULL) {
	    this->gp_sink->grep_begin(*this);
	}
    };

    /** @param gpd The sink to send results to. */
    void set_control(grep_proc_control *gpc)
    {
	this->gp_control = gpc;
    };

    /** @return The sink to send resuls to. */
    grep_proc_sink *get_sink() { return this->gp_sink; };

    /**
     * Queue a request to search the input between the given line numbers.
     *
     * @param start The line number to start the search at.
     * @param stop The line number to stop the search at or -1 to read until
     * the end-of-file.
     */
    void queue_request(grep_line_t start = grep_line_t(0),
		       grep_line_t stop = grep_line_t(-1))
    {
	assert(start != -1 || stop == -1);
	assert(stop == -1 || start < stop);

	this->gp_queue.push_back(std::make_pair(start, stop));
    };

    /**
     * Start the search requests that have been queued up with queue_request.
     */
    void start(void);

    /**
     * Check the fd_set to see if there is any new data to be processed.
     *
     * @param ready_rfds The set of ready-to-read file descriptors.
     */
    void check_fd_set(fd_set &ready_rfds);

    /** Check the invariants for this object. */
    bool invariant(void)
    {
	assert(this->gp_code != NULL);
	if (this->gp_child_started) {
	    assert(this->gp_child > 0);
	    assert(this->gp_line_buffer.get_fd() != -1);
	    assert(FD_ISSET(this->gp_line_buffer.get_fd(), &this->gp_readfds));
	}
	else {
	    assert(this->gp_pipe_offset == 0);
	    // assert(this->gp_child == -1); XXX doesnt work with static destr
	    assert(this->gp_line_buffer.get_fd() == -1);
	}

	return true;
    };

protected:

    /**
     * Dispatch a line received from the child.
     */
    void dispatch_line(char *line);

    /**
     * Free any resources used by the object and make sure the child has been
     * terminated.
     */
    void cleanup(void);

    virtual void child_init(void) { };

    virtual void child_batch(void) { fflush(stdout); };

    virtual void child_term(void) { fflush(stdout); };

    virtual void handle_match(int line,
			      std::string &line_value,
			      int off,
			      int *matches,
			      int count);

    pcrepp gp_pcre;
    pcre *gp_code;                       /*< The compiled pattern. */
    auto_mem<pcre_extra> gp_code_extra;  /*< Results of a pcre_study. */
    grep_proc_source & gp_source;        /*< The data source delegate. */

    auto_fd     gp_err_pipe;             /*< Standard error from the child. */
    line_buffer gp_line_buffer;          /*< Standard out from the child. */
    off_t       gp_pipe_offset;

    pid_t gp_child;			/*<
					 * The child's pid or zero in the
					 * child.
					 */
    bool  gp_child_started;             /*< True if the child was start()'d. */
    int & gp_maxfd;
    fd_set & gp_readfds;		/*<
					 * Pointer to the read fd_set so we can
					 * clear our file descriptors later.
					 */

    /** The queue of search requests. */
    std::deque<std::pair<grep_line_t, grep_line_t> > gp_queue;
    grep_line_t       gp_last_line;	/*<
					 * The last line number received from
					 * the child.  For multiple matches,
					 * the line number is only sent once.
					 */
    grep_proc_sink    *gp_sink;         /*< The sink delegate. */
    grep_proc_control *gp_control;      /*< The control delegate. */
};

#endif
