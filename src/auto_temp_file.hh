/**
 * Copyright (c) 2007-2012, Timothy Stack
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
	    if (mktemp(pat) == NULL)
		perror("mktemp");
	    this->atf_name = pat;
	}
    };
    
private:
    std::string atf_name;
};

#endif
