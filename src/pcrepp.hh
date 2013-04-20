
/**
 * @file pcrepp.hh
 *
 * A C++ adapter for the pcre library.  The interface provided here has a
 * different focus than the pcrecpp.h file included in the pcre distribution.
 * The standard pcrecpp.h interface is more concerned with regular expressions
 * that are digesting data to be used within the program itself.  Whereas this
 * interface is dealing with regular expression entered by the user and
 * processing a series of matches on text files.
 */

#ifndef __pcrepp_hh
#define __pcrepp_hh

#ifdef HAVE_PCRE_H
#include <pcre.h>
#elif HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#error "pcre.h not found?"
#endif

#include <string.h>

#include <string>
#include <memory>
#include <exception>

#include "auto_mem.hh"

#include <stdio.h>

/**
 * Context that tracks captures found during a match operation.  This class is a
 * base that defines iterator methods and fields, but does not allocate space
 * for the capture array.
 */
class pcre_context {
public:
    typedef struct capture {
	capture() : c_begin(-1), c_end(-1) { };
	capture(int begin, int end) : c_begin(begin), c_end(end) {
	    assert(begin <= end);
	};
	
	int c_begin;
	int c_end;

	int length() const { return this->c_end - this->c_begin; };
    } capture_t;
    typedef capture_t *iterator;
    typedef const capture_t *const_iterator;

    /** @return The maximum number of strings this context can capture. */
    int get_max_count() const {
	return this->pc_max_count;
    };

    void set_count(int count) {
	this->pc_count = count;
    };

    int get_count(void) const {
        return this->pc_count;
    };

    /**
     * @return a capture_t that covers all of the text that was matched.
     */
    capture_t *all() { return pc_captures; };

    /** @return An iterator to the first capture. */
    iterator begin() { return pc_captures + 1; };
    /** @return An iterator that refers to the end of the capture array. */
    iterator end() { return pc_captures + pc_count; };
    
protected:
    pcre_context(capture_t *captures, int max_count)
	: pc_captures(captures), pc_max_count(max_count), pc_count(0) { };

    capture_t *pc_captures;
    int pc_max_count;
    int pc_count;
};

/**
 * A pcre_context that allocates storage for the capture array within the object
 * itself.
 */
template<size_t MAX_COUNT>
class pcre_context_static : public pcre_context {
public:
    pcre_context_static()
	: pcre_context(this->pc_match_buffer, MAX_COUNT + 1) { };

private:
    capture_t pc_match_buffer[MAX_COUNT + 1];
};

/**
 * 
 */
class pcre_input {
public:
    pcre_input(const char *str, size_t off = 0, size_t len = -1)
	: pi_offset(off),
	  pi_next_offset(off),
	  pi_length(len),
	  pi_string(str) {
	if (this->pi_length == (size_t)-1)
	    this->pi_length = strlen(str);
    };

    pcre_input(const std::string &str, size_t off = 0)
	: pi_offset(off),
	  pi_next_offset(off),
	  pi_length(str.length()),
	  pi_string(str.c_str()) {
    };

    const char *get_string() const { return this->pi_string; };

    const char *get_substr_start(const pcre_context::iterator iter) const {
	return &this->pi_string[iter->c_begin];
    };

    std::string get_substr(pcre_context::const_iterator iter) const {
	return std::string(this->pi_string,
			   iter->c_begin,
			   iter->length());
    };

    size_t pi_offset;
    size_t pi_next_offset;
    size_t pi_length;
private:
    const char *pi_string;
};

class pcrepp {
public:
    class error : public std::exception {
    public:
	error(std::string msg, int offset) :
	    e_msg(msg), e_offset(offset) { };
	virtual ~error() throw () { };
	
	virtual const char* what() const throw() {
	    return this->e_msg.c_str();
	};
	
	const std::string e_msg;
	int e_offset;
    };

    pcrepp(pcre *code) : p_code(code) {
	const char *errptr;
	
	pcre_refcount(this->p_code, 1);
	this->p_code_extra = pcre_study(this->p_code, 0, &errptr);
        if (!this->p_code_extra && errptr) {
                fprintf(stderr, "pcre_study error: %s\n", errptr);
        }
    };
    
    pcrepp(const char *pattern, int options = 0) {
	const char *errptr;
	int eoff;
	
	if ((this->p_code = pcre_compile(pattern,
					 options,
					 &errptr,
					 &eoff,
					 NULL)) == NULL) {
	    throw error(errptr, eoff);
	}
	
	pcre_refcount(this->p_code, 1);
	this->p_code_extra = pcre_study(this->p_code, 0, &errptr);
        if (!this->p_code_extra && errptr) {
                fprintf(stderr, "pcre_study error: %s\n", errptr);
        }
    };

    pcrepp(const pcrepp &other) {
	const char *errptr;
	
	this->p_code = other.p_code;
	pcre_refcount(this->p_code, 1);
	this->p_code_extra = pcre_study(this->p_code, 0, &errptr);
        if (!this->p_code_extra && errptr) {
                fprintf(stderr, "pcre_study error: %s\n", errptr);
        }
    };

    virtual ~pcrepp() { 
    	if (pcre_refcount(this->p_code, -1) == 0) {
    	    free(this->p_code);
    	    this->p_code = 0;
    	}
    };

    bool match(pcre_context &pc, pcre_input &pi, int options = 0) {
	int count = pc.get_max_count();
	int rc;

	pi.pi_offset = pi.pi_next_offset;
	rc = pcre_exec(this->p_code,
		       this->p_code_extra,
		       pi.get_string(),
		       pi.pi_length,
		       pi.pi_offset,
		       options,
		       (int *)pc.all(),
		       count * 2);

	
	if (rc < 0) {
	}
	else if (rc == 0) {
	    rc = 0;
	}
	else if (pc.all()->c_begin == pc.all()->c_end) {
	    rc = 0;
	}
	else  {
	    pi.pi_next_offset = pc.all()->c_end;
	}

	pc.set_count(rc);
	
	return rc > 0;
    };
    
private:
    pcre *p_code;
    auto_mem<pcre_extra> p_code_extra;
};

#endif
