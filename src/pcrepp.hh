
#ifndef __pcrepp_hh
#define __pcrepp_hh

#ifdef HAVE_PCRE_H
#include <pcre.h>
#elif HAVE_PCRE_PCRE_H
#include <pcre/pcre.h>
#else
#error "pcre.h not found?"
#endif

#include <string>
#include <memory>
#include <exception>

#include "auto_mem.hh"

class pcre_context {
public:
    typedef struct {
	int m_begin;
	int m_end;

	int length() { return this->m_end - this->m_begin; };
    } match_t;
    typedef match_t *iterator;

    int get_max_count() {
	return this->pc_max_count;
    };

    void set_count(int count) {
	this->pc_count = count;
    }

    match_t *all() { return pc_matches; };

    iterator begin() { return pc_matches + 1; };
    iterator end() { return pc_matches + pc_count; };
    
protected:
    pcre_context(match_t *matches, int max_count)
	: pc_matches(matches), pc_max_count(max_count) { };

    match_t *pc_matches;
    int pc_max_count;
    int pc_count;
};

template<size_t MAX_COUNT>
class pcre_context_static : public pcre_context {
public:
    pcre_context_static()
	: pcre_context(this->pc_match_buffer, MAX_COUNT + 1) { };

private:
    match_t pc_match_buffer[MAX_COUNT + 1];
};

class pcre_input {
public:
    pcre_input(const char *str, size_t off = 0, size_t len = -1)
	: pi_string(str), pi_offset(off), pi_length(len), pi_next_offset(off) {
	if (this->pi_length == -1)
	    this->pi_length = strlen(str);
    };

    pcre_input(std::string &str, size_t off = 0)
	: pi_string(str.c_str()), pi_offset(off), pi_length(str.length()),
	  pi_next_offset(off) {
    };

    const char *get_string() const { return this->pi_string; };

    const char *get_substr(pcre_context::iterator iter) const {
	return &this->pi_string[iter->m_begin];
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
	
	this->p_code_extra = pcre_study(this->p_code, 0, &errptr);
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
	
	this->p_code_extra = pcre_study(this->p_code, 0, &errptr);
    };

    virtual ~pcrepp() { };

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
	else if (pc.all()->m_begin == pc.all()->m_end)
	    rc = 0;
	else 
	    pi.pi_next_offset = pc.all()->m_end;

	pc.set_count(rc);
	
	return rc > 0;
    };
    
private:
    pcre *p_code;
    auto_mem<pcre_extra> p_code_extra;
};

#endif
