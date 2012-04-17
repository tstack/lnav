/**
 * @file line_buffer.hh
 */

#ifndef __line_buffer_hh
#define __line_buffer_hh

#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#include <exception>

#include "auto_fd.hh"
#include "auto_mem.hh"

/**
 * Buffer for reading whole lines out of file descriptors.  The class presents
 * a stateless interface, callers specify the offset where a line starts and
 * the class takes care of caching the surrounding range and locating the
 * delimiter.
 *
 * XXX A bit of a wheel reinvention, but I'm not sure how well the libraries
 * handle non-blocking I/O...
 */
class line_buffer {
public:
    class error
	: public std::exception {
public:
	error(int err)
	    : e_err(err) { };

	int e_err;
    };

    /** Construct an empty line_buffer. */
    line_buffer();

    virtual ~line_buffer();

    /** @param fd The file descriptor that data should be pulled from. */
    void set_fd(auto_fd &fd) throw (error);

    /** @return The file descriptor that data should be pulled from. */
    int get_fd() { return this->lb_fd; };

    /**
     * @return The size of the file or the amount of data pulled from a pipe.
     */
    size_t get_file_size() { return this->lb_file_size; };

    off_t get_read_offset(off_t off) {
	if (this->lb_gz_file)
	    return this->lb_gz_offset;
	else
	    return off;
    };

    /**
     * Read up to the end of file or a given delimiter.
     *
     * @param offset_inout The offset in the file to start reading from.  On
     * return, it contains the offset where the next line should start or one
     * past the size of the file.
     * @param len_out On return, contains the length of the line, not including
     * the delimiter.
     * @param delim The character that splits lines in the input, defaults to a
     * line feed.
     * @return The address in the internal buffer where the line starts.  The
     * line is not terminated, but this method ensures there is room to NULL
     * terminate the line.  If any modifications are made to the line, such as
     * NULL termination, the invalidate() must be called before re-reading the
     * line to refresh the buffer.
     */
    char *read_line(off_t &offset_inout, size_t &len_out, char delim = '\n')
    throw (error);

    /**
     * Signal that the contents of the internal buffer have been modified and
     * any attempts to re-read the currently cached line(s) should trigger
     * another read from the file.
     */
    void invalidate()
    {
	this->lb_file_offset += this->lb_buffer_size;
	this->lb_buffer_size  = 0;
    };

    /** Release any resources held by this object. */
    void reset()
    {
	this->lb_fd.reset();

	this->lb_file_offset = 0;
	this->lb_file_size   = (size_t)-1;
	this->lb_buffer_size = 0;
    };

    /** Check the invariants for this object. */
    bool invariant(void)
    {
	assert(this->lb_buffer != NULL);
	assert(this->lb_buffer_size <= this->lb_buffer_max);

	return true;
    };

private:

    /**
     * @param off The file offset to check for in the buffer.
     * @return True if the given offset is cached in the buffer.
     */
    bool in_range(off_t off)
    {
	return this->lb_file_offset <= off &&
	       off < (int)(this->lb_file_offset + this->lb_buffer_size);
    };

    void resize_buffer(size_t new_max) throw (error);

    /**
     * Ensure there is enough room in the buffer to cache a range of data from
     * the file.  First, this method will check to see if there is enough room
     * from where 'start' begins in the buffer to the maximum buffer size.  If
     * this is not enough, the currently cached data at 'start' will be moved
     * to the beginning of the buffer, overwriting any cached data earlier in
     * the file.  Finally, if this is still not enough, the buffer will be
     * reallocated to make more room.
     *
     * @param start The file offset of the start of the line.
     * @param max_length The amount of data to be cached in the buffer.
     */
    void ensure_available(off_t start, size_t max_length) throw (error);

    /**
     * Fill the buffer with the given range of data from the file.
     *
     * @param start The file offset where data should start to be read from the
     * file.
     * @param max_length The maximum amount of data to read from the file.
     * @return True if any data was read from the file.
     */
    bool fill_range(off_t start, size_t max_length) throw (error);

    /**
     * After a successful fill, the cached data can be retrieved with this
     * method.
     *
     * @param start The file offset to retrieve cached data for.
     * @param avail_out On return, the amount of data currently cached at the
     * given offset.
     * @return A pointer to the start of the cached data in the internal
     * buffer.
     */
    char *get_range(off_t start, size_t &avail_out)
    {
	off_t  buffer_offset = start - this->lb_file_offset;
	char *retval;

	assert(buffer_offset >= 0);

	retval    = &this->lb_buffer[buffer_offset];
	avail_out = this->lb_buffer_size - buffer_offset;

	return retval;
    };

    auto_fd lb_fd;              /*< The file to read data from. */
    gzFile lb_gz_file;
    bool lb_bz_file;
    off_t lb_gz_offset;
    
    auto_mem<char> lb_buffer;   /*< The internal buffer where data is cached */

    size_t lb_file_size;	/*<
				 * The size of the file.  When lb_fd refers to
				 * a pipe, this is set to the amount of data
				 * read from the pipe when EOF is reached.
				 */
    off_t  lb_file_offset;	/*<
				 * Data cached in the buffer comes from this
				 * offset in the file.
				 */
    size_t lb_buffer_size;      /*< The amount of cached data in the buffer. */
    size_t lb_buffer_max;       /*< The size of the buffer memory. */
    bool   lb_seekable;
};

#endif
