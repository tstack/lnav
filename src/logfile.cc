/**
 * @file logfile.cc
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <time.h>

#include "logfile.hh"

using namespace std;

logfile::logfile(string filename, auto_fd fd)
throw (error)
    : lf_filename(filename),
      lf_index_time(0),
      lf_index_size(0)
{
    int reserve_size = 100;

    assert(filename.size() > 0);
    
    if (fd == -1) {
	char resolved_path[PATH_MAX];
	struct stat st;
	
	errno = 0;
	if (realpath(filename.c_str(), resolved_path) == NULL) {
	    throw error(resolved_path, errno);
	}
	filename = resolved_path;
	
	if (stat(filename.c_str(), &st) == -1) {
	    throw error(filename, errno);
	}
	reserve_size = st.st_size / 100;
	
	if (!S_ISREG(st.st_mode)) {
	    throw error(filename, EINVAL);
	}

	if ((fd = open(filename.c_str(), O_RDONLY)) == -1) {
	    throw error(filename, errno);
	}
    }

    this->lf_line_buffer.set_fd(fd);
    this->lf_index.reserve(reserve_size);

    assert(this->invariant());
}

logfile::~logfile()
{ }

void logfile::process_prefix(off_t offset, char *prefix, int len)
{
    bool found = false;

    if (this->lf_format.get() != NULL) {
	/* We've locked onto a format, just use that scanner. */
	found = this->lf_format->scan(this->lf_index, offset, prefix, len);
    }
    else {
	vector<log_format *> &root_formats = log_format::get_root_formats();
	vector<log_format *>::iterator iter;
	
	/*
	 * Try each scanner until we get a match.  Fortunately, all the formats
	 * are sufficiently different that there are no ambiguities...
	 */
	for (iter = root_formats.begin();
	     iter != root_formats.end() && !found;
	     iter++) {
	    (*iter)->clear();
	    if ((*iter)->scan(this->lf_index, offset, prefix, len)) {
#if 0
		assert(this->lf_index.size() == 1 ||
		       (this->lf_index[this->lf_index.size() - 2] <
			this->lf_index[this->lf_index.size() - 1]));
#endif

		this->lf_format =
		    auto_ptr<log_format>((*iter)->specialized());
		found           = true;
	    }
	}
    }

    /* If the scanner didn't match, than we need to add it. */
    if (!found) {
	logline::level_t last_level  = logline::LEVEL_UNKNOWN;
	time_t           last_time   = this->lf_index_time;
	short            last_millis = 0;

	if (!this->lf_index.empty()) {
	    logline &ll = this->lf_index.back();

	    /*
	     * Assume this line is part of the previous one(s) and copy the
	     * metadata over.
	     */
	    ll.set_multiline();
	    last_time   = ll.get_time();
	    last_millis = ll.get_millis();
	    if (this->lf_format.get() != NULL) {
		last_level = (logline::level_t)
			     (ll.get_level() | logline::LEVEL_CONTINUED);
	    }
	}
	this->lf_index.push_back(logline(offset,
					 last_time,
					 last_millis,
					 last_level));
    }
}

bool logfile::rebuild_index(logfile_observer *lo)
throw (line_buffer::error)
{
    bool        retval = false;
    struct stat st;

    if (fstat(this->lf_line_buffer.get_fd(), &st) == -1) {
	throw error(this->lf_filename, errno);
    }

    /* Check for new data based on the file size. */
    if (this->lf_index_size < st.st_size) {
	off_t  last_off, off;
	char   *line;
	size_t len;

	this->lf_line_buffer.set_file_size((size_t)-1);
	if (this->lf_index.size() > 0) {
	    off = this->lf_index.back().get_offset();

	    /*
	     * Drop the last line we read since it might have been a partial
	     * read.
	     */
	    this->lf_index.pop_back();
	}
	else {
	    off = 0;
	}
	last_off = off;
	while ((line = this->lf_line_buffer.read_line(off, len)) != NULL) {
	    line[len] = '\0';
	    this->process_prefix(last_off, line, len);
	    last_off = off;

	    if (lo != NULL) {
		lo->logfile_indexing(*this,
				     this->lf_line_buffer.get_read_offset(off),
				     st.st_size);
	    }
	}

	this->lf_line_buffer.invalidate();

	/*
	 * The file can still grow between the above fstat and when we're
	 * doing the scanning, so use the line buffer's notion of the file
	 * size.
	 */
	this->lf_index_size = this->lf_line_buffer.get_file_size();

	retval = true;
    }

    this->lf_index_time = st.st_mtime;

    return retval;
}

void logfile::read_line(logfile::iterator ll, string &line_out)
{
    try {
	off_t      off = ll->get_offset();
	const char *line;
	size_t     len;

	line_out.clear();
	if ((line = this->lf_line_buffer.read_line(off, len)) != NULL) {
	    line_out.append(line, len);
	}
	else {
	    /* XXX */
	}
    }
    catch (line_buffer::error & e) {
	/* ... */
    }
}
