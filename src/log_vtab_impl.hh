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

#ifndef __vtab_impl_hh
#define __vtab_impl_hh

#include <sqlite3.h>

#include <map>
#include <string>
#include <vector>

#include "listview_curses.hh"
#include "logfile_sub_source.hh"

enum {
    VT_COL_LINE_NUMBER,
    VT_COL_PATH,
    VT_COL_LOG_TIME,
    VT_COL_IDLE_MSECS,
    VT_COL_LEVEL,
    VT_COL_RAW_LINE,
    VT_COL_MAX
};

class logfile_sub_source;

struct log_cursor {
    vis_line_t lc_curr_line;
    int lc_sub_index;
};

class log_vtab_impl {
public:
    struct vtab_column {
	vtab_column(const char *name, const char *type)
	    : vc_name(name), vc_type(type) { };
	
	const char *vc_name;
	const char *vc_type;
    };
    
    log_vtab_impl(const std::string name)
	: vi_name(name) {
    };
    virtual ~log_vtab_impl() { };
    
    const std::string &get_name(void) const {
	return this->vi_name;
    };

    virtual bool next(log_cursor &lc, logfile_sub_source &lss) {
	lc.lc_curr_line = lc.lc_curr_line + vis_line_t(1);
	lc.lc_sub_index = 0;

	if (lc.lc_curr_line == (int)lss.text_line_count())
	    return true;
	
	content_line_t cl(lss.at(lc.lc_curr_line));
	logfile *lf = lss.find(cl);

	log_format *format = lf->get_format();
	if (format != NULL && format->get_name() == this->vi_name)
	    return true;

	return false;
    };
    
    virtual void get_columns(std::vector<vtab_column> &cols) { };

    virtual void extract(const std::string &line,
			 int column,
			 sqlite3_context *ctx) {
    };
    
private:
    const std::string vi_name;
};

class log_vtab_manager {
public:
    log_vtab_manager(sqlite3 *db, logfile_sub_source &lss);

    logfile_sub_source *get_source() { return &this->vm_source; };
    
    void register_vtab(log_vtab_impl *vi);
    log_vtab_impl *lookup_impl(std::string name) {
	return this->vm_impls[name];
    };
    
private:
    sqlite3 *vm_db;
    logfile_sub_source &vm_source;
    std::map<std::string, log_vtab_impl *> vm_impls;
};

#endif
