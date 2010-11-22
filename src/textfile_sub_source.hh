
#ifndef __textfile_sub_source_hh
#define __textfile_sub_source_hh

#include <list>

#include "logfile.hh"
#include "textview_curses.hh"

class textfile_sub_source : public text_sub_source {
public:
    typedef std::list<logfile *>::iterator file_iterator;
    
    textfile_sub_source() { };

    size_t text_line_count() {
	size_t retval = 0;
	
	if (!this->tss_files.empty()) {
	    retval = this->current_file()->size();
	}

	return retval;
    };

    void text_value_for_line(textview_curses &tc,
			     int line,
			     std::string &value_out,
			     bool raw = false) {
	if (!this->tss_files.empty()) {
	    this->current_file()->
		read_line(this->current_file()->begin() + line, value_out);
	}
	else {
	    value_out.clear();
	}
    };

    void text_attrs_for_line(textview_curses &tc,
			     int row,
			     string_attrs_t &value_out) {
	if (this->current_file() == NULL)
	    return;
	
	struct line_range lr;

	lr.lr_start = 0;
	lr.lr_end = -1;
	value_out[lr].insert(make_string_attr("file", this->current_file()));
    };

    logfile *current_file(void) const {
	if (this->tss_files.empty())
	    return NULL;

	return (*this->tss_files.begin());
    };

    std::list<logfile *> tss_files;
};

#endif
