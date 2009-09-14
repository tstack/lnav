
#ifndef __auto_temp_file_hh
#define __auto_temp_file_hh

#include <unistd.h>

#include <string>

class auto_temp_file {
public:
    auto_temp_file(const char *cpat = NULL) {
	this->reset(cpat);
    };

    auto_temp_file(auto_temp_file &atf) : atf_name(atf.release()) { };

    ~auto_temp_file() { this->reset(); };

    operator std::string(void) const { return this->atf_name; };

    auto_temp_file &operator=(const char *cpat) {
	this->reset(cpat);
	return *this;
    };
    
    auto_temp_file &operator=(auto_temp_file &atf) {
	this->reset();
	this->atf_name = atf.release();
	return *this;
    };

    std::string release(void) {
	std::string retval = this->atf_name;

	this->atf_name.clear();
	return retval;
    };
    
    void reset(const char *cpat = NULL) {
	if (!this->atf_name.empty()) {
	    unlink(this->atf_name.c_str());
	    this->atf_name.clear();
	}
	if (cpat != NULL) {
	    char *pat = (char *)alloca(strlen(cpat) + 1); /* XXX */

	    strcpy(pat, cpat);
	    mktemp(pat);
	    this->atf_name = pat;
	}
    };
    
private:
    std::string atf_name;
};

#endif
