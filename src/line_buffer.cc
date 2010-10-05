/**
 * @file line_buffer.cc
 */

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <set>

#include "line_buffer.hh"

using namespace std;

static const size_t DEFAULT_LINE_BUFFER_SIZE = 256 * 1024;
static const size_t MAX_LINE_BUFFER_SIZE     = 2 * DEFAULT_LINE_BUFFER_SIZE;
static const size_t DEFAULT_INCREMENT        = 1024;

static set<line_buffer *> ALL_BUFFERS;

/*
 * XXX REMOVE ME
 *
 * The stock gzipped file code does not use pread, so we need to use a lock to
 * get exclusive access to the file.  In the future, we should just rewrite
 * the gzipped file code to use pread.
 */
class lock_hack {
public:
    class guard {
    public:
	
	guard() : g_lock(lock_hack::singleton()) {
	    this->g_lock.lock();
	};

	~guard() {
	    this->g_lock.unlock();
	};

    private:
	lock_hack &g_lock;
    };

    static lock_hack &singleton() {
	static lock_hack retval;

	return retval;
    };

    void lock() {
	lockf(this->lh_fd, F_LOCK, 0);
    };

    void unlock() {
	lockf(this->lh_fd, F_ULOCK, 0);
    };

private:
    
    lock_hack() {
	char lockname[64];
	
	snprintf(lockname, sizeof(lockname), "/tmp/lnav.%d.lck", getpid());
	this->lh_fd = open(lockname, O_CREAT | O_RDWR, 0600);
	unlink(lockname);
    };

    int lh_fd;
};
/* XXX END */

line_buffer::line_buffer()
    : lb_gz_file(NULL),
      lb_file_size((size_t) - 1),
      lb_file_offset(0),
      lb_buffer_size(0),
      lb_buffer_max(DEFAULT_LINE_BUFFER_SIZE),
      lb_seekable(false)
{
    if ((this->lb_buffer = (char *)malloc(this->lb_buffer_max)) == NULL) {
	throw bad_alloc();
    }

    ALL_BUFFERS.insert(this);

    assert(this->invariant());
}

line_buffer::~line_buffer()
{
    auto_fd fd = -1;

    this->set_fd(fd);
    
    ALL_BUFFERS.erase(this);
}

void line_buffer::set_fd(auto_fd &fd)
throw (error)
{
    off_t newoff = 0;

    if (this->lb_gz_file) {
	gzclose(this->lb_gz_file);
	this->lb_gz_file = NULL;
    }

    if (fd != -1) {
	/* Sync the fd's offset with the object. */
	newoff = lseek(fd, 0, SEEK_CUR);
	if (newoff == -1) {
	    if (errno != ESPIPE) {
		throw error(errno);
	    }

	    /* It's a pipe, start with a zero offset. */
	    newoff            = 0;
	    this->lb_seekable = false;
	}
	else {
	    char gz_id[2];

	    if (pread(fd, gz_id, sizeof(gz_id), 0) == sizeof(gz_id)) {
		if (gz_id[0] == '\037' && gz_id[1] == '\213') {
		    lseek(fd, 0, SEEK_SET);
		    if ((this->lb_gz_file = gzdopen(dup(fd), "r")) == NULL) {
			if (errno == 0)
			    throw bad_alloc();
			else
			    throw error(errno);
		    }
		    this->lb_gz_offset = lseek(this->lb_fd, 0, SEEK_CUR);
		}
	    }
	    this->lb_seekable = true;
	}
    }
    this->lb_file_offset = newoff;
    this->lb_buffer_size = 0;
    this->lb_fd          = fd;

    assert(this->invariant());
}

void line_buffer::ensure_available(off_t start, size_t max_length)
throw (error)
{
    size_t prefill, available;

    /* The file is probably bogus if a line has gotten this big. */
    if (max_length > MAX_LINE_BUFFER_SIZE) {
	throw error(EFBIG);
    }

    if (start < this->lb_file_offset ||
	start >= (off_t)(this->lb_file_offset + this->lb_buffer_size)) {
	/*
	 * The request is outside the cached range, need to reload the
	 * whole thing.
	 */
	prefill = 0;
	this->lb_file_offset = start;
	this->lb_buffer_size = 0;
    }
    else {
	/* The request is in the cached range. */
	prefill = start - this->lb_file_offset;
    }
    assert(this->lb_file_offset <= start);
    assert(prefill <= this->lb_buffer_size);

    available = this->lb_buffer_max - this->lb_buffer_size;
    assert(available <= this->lb_buffer_max);

    if (max_length > available) {
	/*
	 * Need more space, move any existing data to the front of the
	 * buffer.
	 */
	this->lb_buffer_size -= prefill;
	this->lb_file_offset += prefill;
	memmove(&this->lb_buffer[0],
		&this->lb_buffer[prefill],
		this->lb_buffer_size);

	available = this->lb_buffer_max - this->lb_buffer_size;
	if (max_length > available) {
	    char *tmp, *old;

	    /* Still need more space, try a realloc. */
	    old = this->lb_buffer.release();
	    tmp = (char *)realloc(old,
				  this->lb_buffer_max +
				  DEFAULT_LINE_BUFFER_SIZE);
	    if (tmp != NULL) {
		this->lb_buffer      = tmp;
		this->lb_buffer_max += DEFAULT_LINE_BUFFER_SIZE;
	    }
	    else {
		this->lb_buffer = old;

		throw error(ENOMEM);
	    }
	}
    }
}

bool line_buffer::fill_range(off_t start, size_t max_length)
throw (error)
{
    bool retval = false;

    if (this->in_range(start) && this->in_range(start + max_length)) {
	/* Cache already has the data, nothing to do. */
	retval = true;
    }
    else if (this->lb_fd != -1) {
	int rc;

	/* Make sure there is enough space, then */
	this->ensure_available(start, max_length);

	/* ... read in the new data. */
	if (this->lb_gz_file) {
	    lock_hack::guard guard;
	    
	    lseek(this->lb_fd, this->lb_gz_offset, SEEK_SET);
	    gzseek(this->lb_gz_file,
		   this->lb_file_offset + this->lb_buffer_size,
		   SEEK_SET);
	    rc = gzread(this->lb_gz_file,
			&this->lb_buffer[this->lb_buffer_size],
			this->lb_buffer_max - this->lb_buffer_size);
	    this->lb_gz_offset = lseek(this->lb_fd, 0, SEEK_CUR);
	}
	else if (this->lb_seekable) {
	    rc = pread(this->lb_fd,
		       &this->lb_buffer[this->lb_buffer_size],
		       this->lb_buffer_max - this->lb_buffer_size,
		       this->lb_file_offset + this->lb_buffer_size);
	}
	else {
	    rc = read(this->lb_fd,
		      &this->lb_buffer[this->lb_buffer_size],
		      this->lb_buffer_max - this->lb_buffer_size);
	}
	switch (rc) {
	case 0:
	    this->lb_file_size = this->lb_file_offset + this->lb_buffer_size;
	    if (start < (off_t) this->lb_file_size) {
		retval = true;
	    }
	    break;

	case - 1:
	    switch (errno) {
	    case EINTR:
	    case EAGAIN:
		break;

	    default:
		throw error(errno);
	    }
	    break;

	default:
	    this->lb_buffer_size += rc;
	    retval = true;
	    break;
	}

	assert(this->lb_buffer_size <= this->lb_buffer_max);
    }

    return retval;
}

char *line_buffer::read_line(off_t &offset, size_t &len_out, char delim)
throw (error)
{
    size_t request_size = DEFAULT_INCREMENT;
    char   *retval      = NULL;

    assert(this->lb_fd != -1);

    len_out = 0;
    while ((retval == NULL) && this->fill_range(offset, request_size)) {
	char *line_start, *lf;

	/* Find the data in the cache and */
	line_start = this->get_range(offset, len_out);
	/* ... look for the end-of-line or end-of-file. */
	if (((lf = (char *)memchr(line_start, delim, len_out)) != NULL) ||
	    ((request_size >= (MAX_LINE_BUFFER_SIZE - DEFAULT_INCREMENT)) &&
	     (offset + len_out) == this->lb_file_size)) {
	    if (lf != NULL) {
		len_out = lf - line_start;
		offset += 1; /* Skip the delimiter. */
	    }
	    else {
		/*
		 * Be nice and make sure there is room for the caller to
		 * add a NULL-terminator.
		 */
		this->ensure_available(offset, len_out + 1);
	    }
	    offset += len_out;

	    retval = line_start;
	}
	else {
	    request_size += DEFAULT_INCREMENT;
	}
    }

    assert((retval == NULL) ||
	   (retval >= this->lb_buffer &&
	    retval < (this->lb_buffer + this->lb_buffer_size)));
    assert(len_out <= this->lb_buffer_size);
    assert(this->invariant());

    return retval;
}
