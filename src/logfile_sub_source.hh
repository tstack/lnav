#ifndef __logfile_controller_hh
#define __logfile_controller_hh

#include <map>
#include <list>
#include <sstream>
#include <vector>
#include <algorithm>

#include "strong_int.hh"
#include "logfile.hh"
#include "bookmarks.hh"
#include "textview_curses.hh"

STRONG_INT_TYPE(int, content_line);

class logfile_filter {
public:
    typedef enum {
	MAYBE,
	INCLUDE,
	EXCLUDE
    } type_t;

    logfile_filter(type_t type, std::string id)
	: lf_enabled(true),
	  lf_type(type),
	  lf_id(id) { };
    virtual ~logfile_filter() { };

    type_t get_type(void) const { return this->lf_type; };
    std::string get_id(void) const { return this->lf_id; };

    bool is_enabled(void) { return this->lf_enabled; };
    void enable(void) { this->lf_enabled = true; };
    void disable(void) { this->lf_enabled = false; };

    virtual bool matches(std::string line) = 0;

protected:
    bool        lf_enabled;
    type_t      lf_type;
    std::string lf_id;
};

class logfile_sub_source
    : public text_sub_source {
public:

    typedef std::vector<logfile_filter *> filter_stack_t;

    class observer
	: public logfile_observer {
public:
	virtual void logfile_sub_source_filtering(logfile_sub_source &lss,
						  content_line_t cl,
						  size_t total) = 0;
    };

    static bookmark_type_t BM_ERRORS;
    static bookmark_type_t BM_WARNINGS;
    static bookmark_type_t BM_FILES;

    logfile_sub_source();
    virtual ~logfile_sub_source();

    filter_stack_t &get_filters(void)
    {
	return this->lss_filters;
    };

    logfile_filter *get_filter(std::string id)
    {
	filter_stack_t::iterator iter;
	logfile_filter           *retval = NULL;

	for (iter = this->lss_filters.begin();
	     iter != this->lss_filters.end() && (*iter)->get_id() != id;
	     iter++) { }
	if (iter != this->lss_filters.end()) {
	    retval = *iter;
	}

	return retval;
    };

    void toggle_scrub(void) { this->lss_flags ^= F_NO_SCRUB; };
    size_t text_line_count()
    {
	return this->lss_index.size();
    };
    void text_value_for_line(textview_curses &tc,
			     int row,
			     std::string &value_out,
			     bool raw);
    
    void text_attrs_for_line(textview_curses &tc,
			     int row,
			     string_attrs_t &value_out);

    void text_user_mark(bookmark_type_t *bm, int line, bool added)
    {
	content_line_t cl = this->lss_index[line];
	std::vector<content_line_t>::iterator lb;

	lb = std::lower_bound(this->lss_user_marks[bm].begin(),
			      this->lss_user_marks[bm].end(),
			      cl);
	if (added) {
	    this->lss_user_marks[bm].insert(lb, cl);
	}
	else {
	    assert(lb != this->lss_user_marks[bm].end());
	    
	    this->lss_user_marks[bm].erase(lb);
	}
    };

    void insert_file(logfile *lf)
    {
	assert(lf->size() < MAX_LINES_PER_FILE);
	
	this->lss_files.push_back(logfile_data(lf));
	this->lss_index.clear();
    };
    bool rebuild_index(observer *obs = NULL, bool force = false);

    void text_update_marks(vis_bookmarks &bm);

    void toggle_user_mark(bookmark_type_t *bm,
			  vis_line_t start_line,
			  vis_line_t end_line = vis_line_t(-1))
    {
	if (end_line == -1)
	    end_line = start_line;
	for (vis_line_t curr_line = start_line; curr_line <= end_line; ++curr_line) {
	    bookmark_vector<content_line_t> &bv = this->lss_user_marks[bm];
	    bookmark_vector<content_line_t>::iterator iter;
	    
	    iter = bv.insert_once(this->at(curr_line));
	    if (iter == bv.end()) {
	    }
	    else {
		bv.erase(iter);
	    }
	}
    };

    bookmarks<content_line_t>::type &get_user_bookmarks(void) {
    	return this->lss_user_marks;
    };

    int get_filtered_count() const { return this->lss_filtered_count; };

    logfile *find(const char *fn, content_line_t &line_base);

    logfile *find(content_line_t &line)
    {
	logfile *retval;

	retval = this->lss_files[line / MAX_LINES_PER_FILE].ld_file;
	line   = content_line_t(line % MAX_LINES_PER_FILE);

	return retval;
    };

    logline *find_line(content_line_t line)
    {
	logline *retval = NULL;
	logfile *lf     = this->find(line);

	if (lf != NULL) {
	    logfile::iterator ll_iter = lf->begin() + line;

	    retval = &(*ll_iter);
	}

	return retval;
    };

    vis_line_t find_from_time(time_t start);

    content_line_t at(vis_line_t vl) { return this->lss_index[vl]; };

private:
    static const size_t MAX_LINES_PER_FILE = 16 * 1024 * 1024;

    enum {
	B_NO_SCRUB,
    };

    enum {
	F_NO_SCRUB = (1L << B_NO_SCRUB),
    };

    struct logline_cmp {
	logline_cmp(logfile_sub_source & lc)
	    : llss_controller(lc) { };
	bool operator()(const content_line_t &lhs, const content_line_t &rhs)
	{
	    logline *ll_lhs = this->llss_controller.find_line(lhs);
	    logline *ll_rhs = this->llss_controller.find_line(rhs);

	    return (*ll_lhs) < (*ll_rhs);
	};
	bool operator()(const content_line_t &lhs, const time_t &rhs)
	{
	    logline *ll_lhs = this->llss_controller.find_line(lhs);

	    return ll_lhs->get_time() < rhs;
	};

	logfile_sub_source & llss_controller;
    };

    struct logfile_data {
	logfile_data(logfile *lf = NULL)
	    : ld_file(lf),
	      ld_lines_indexed(0) { };

	bool operator<(const logfile_data &rhs)
	{
	    return (*this->ld_file) < (*rhs.ld_file);
	};

	logfile *ld_file;
	size_t  ld_lines_indexed;
    };

    unsigned long             lss_flags;
    std::vector<logfile_data> lss_files;

    filter_stack_t lss_filters;
    int            lss_filtered_count;

    std::vector<content_line_t> lss_index;

    bookmarks<content_line_t>::type lss_user_marks;

    logfile           *lss_token_file;
    std::string       lss_token_value;
    int               lss_scrub_len;
    int               lss_token_offset;
    int               lss_token_date_end;
    logfile::iterator lss_token_line;
};

#endif
