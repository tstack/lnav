/**
 * @file grapher.hh
 */

#ifndef _grapher_hh
#define _grapher_hh

#include "config.h"

#include <string>
#include <vector>

#include "grep_proc.hh"
#include "hist_source.hh"
#include "textview_curses.hh"

class grapher
    : public grep_proc_sink,
      public hist_source {
public:

    grapher()
	: gr_highlighter(NULL)
    {
	this->set_label_source(&this->gr_label_source);
    };

    grep_line_t at(int row) { return this->gr_lines[row]; };

    void set_highlighter(textview_curses::highlighter *hl)
    {
	this->gr_highlighter = hl;
    };

    void grep_begin(grep_proc &gp)
    {
	this->clear();
	this->hs_type2role.clear();
	this->gr_lines.clear();
	this->gr_x          = -1;
	this->gr_next_field = bucket_type_t(0);
    };

    void grep_match(grep_proc &gp, grep_line_t line, int start, int end) { };

    void grep_capture(grep_proc &gp,
		      grep_line_t line,
		      int start,
		      int end,
		      char *capture)
    {
	float amount = 1.0;

	if (this->gr_lines.empty() || this->gr_lines.back() != line) {
	    this->gr_next_field = bucket_type_t(0);
	    this->gr_x         += 1;
	    this->gr_lines.push_back(line);
	}

	if (this->gr_highlighter != NULL) {
	    if (this->hs_type2role.find(this->gr_next_field) ==
		this->hs_type2role.end()) {
		this->hs_type2role[this->gr_next_field] =
		    this->gr_highlighter->get_role(this->gr_next_field);
	    }
	}
	if (capture != 0)
	    sscanf(capture, "%f", &amount);
	this->add_value(this->gr_x, this->gr_next_field, amount);

	++ this->gr_next_field;
    };

    void grep_end_batch(grep_proc &gp) { this->analyze(); };
    void grep_end(grep_proc &gp) { this->analyze(); };

private:

    class label_source
	: public hist_source::label_source {
public:
	label_source() { };

	void hist_label_for_bucket(int bucket_start_value,
				   const hist_source::bucket_t &bucket,
				   std::string &label_out)
	{
	    hist_source::bucket_t::const_iterator iter;

	    for (iter = bucket.begin(); iter != bucket.end(); iter++) {
		char buffer[64];

		if (iter->second != 0.0) {
		    snprintf(buffer, sizeof(buffer), "  %10.2f", iter->second);
		}
		else {
		    snprintf(buffer, sizeof(buffer), "  %10s", "-");
		}
		label_out += std::string(buffer);
	    }
	};
    };

    label_source gr_label_source;
    textview_curses::highlighter *gr_highlighter;
    std::vector<grep_line_t> gr_lines;
    int           gr_x;
    bucket_type_t gr_next_field;
};

#endif
