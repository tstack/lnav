/**
 * @file textview_curses.hh
 */

#ifndef __textview_curses_hh
#define __textview_curses_hh

#include <vector>

#include "grep_proc.hh"
#include "bookmarks.hh"
#include "listview_curses.hh"

class textview_curses;

/**
 * Source for the text to be shown in a textview_curses view.
 */
class text_sub_source {
public:
    virtual ~text_sub_source() { };

    virtual void toggle_scrub(void) { };

    /**
     * @return The total number of lines available from the source.
     */
    virtual size_t text_line_count() = 0;

    /**
     * Get the value for a line.
     * 
     * @param tc The textview_curses object that is delegating control.
     * @param line The line number to retrieve.
     * @param value_out The string object that should be set to the line
     *   contents.
     * @param raw Indicates that the raw contents of the line should be returned
     *   without any post processing.
     */
    virtual void text_value_for_line(textview_curses &tc,
				     int line,
				     std::string &value_out,
				     bool raw = false) = 0;

    /**
     * Get the attributes for a line of text.
     * 
     * @param tc The textview_curses object that is delegating control.
     * @param line The line number to retrieve.
     * @param value_out A string_attrs_t object that should be updated with the
     *   attributes for the line.
     */
    virtual void text_attrs_for_line(textview_curses &tc,
				     int line,
				     string_attrs_t &value_out) {
    };

    virtual void text_update_marks(vis_bookmarks &bm) { };
};

/**
 * The textview_curses class adds user bookmarks and searching to the standard
 * list view interface.
 */
class textview_curses
    : public listview_curses,
      public list_data_source,
      public grep_proc_source,
      public grep_proc_sink {
public:

    typedef view_action<textview_curses> action;

    static bookmark_type_t BM_USER;
    static bookmark_type_t BM_SEARCH;

    struct highlighter {
	highlighter()
	    : h_code(NULL),
	      h_multiple(false) { };
	highlighter(pcre *code,
		    bool multiple = false,
		    view_colors::role_t role = view_colors::VCR_NONE)
	    : h_code(code),
	      h_multiple(multiple)
	{
	    if (!multiple) {
		if (role == view_colors::VCR_NONE) {
		    this->h_roles.
		    push_back(view_colors::singleton().next_highlight());
		}
		else {
		    this->h_roles.push_back(role);
		}
	    }
	};

	view_colors::role_t get_role(unsigned int index)
	{
	    view_colors &vc = view_colors::singleton();
	    view_colors::role_t retval;

	    if (this->h_multiple) {
		while (index >= this->h_roles.size()) {
		    this->h_roles.push_back(vc.next_highlight());
		}
		retval = this->h_roles[index];
	    }
	    else {
		retval = this->h_roles[0];
	    }

	    return retval;
	};

	int get_attrs(int index)
	{
	    return view_colors::singleton().
		   attrs_for_role(this->get_role(index));
	};

	pcre                             *h_code;
	bool                             h_multiple;
	std::vector<view_colors::role_t> h_roles;
    };

    textview_curses();
    virtual ~textview_curses();

    vis_bookmarks &get_bookmarks(void) { return this->tc_bookmarks; };

    void set_sub_source(text_sub_source *src)
    {
	this->tc_sub_source = src;
	this->reload_data();
    };
    text_sub_source *get_sub_source(void) { return this->tc_sub_source; };

    void horiz_shift(vis_line_t start, vis_line_t end,
		     int off_start,
		     std::string highlight_name,
		     std::pair<int, int> &range_out) {
	highlighter &hl = this->tc_highlights[highlight_name];
	int prev_hit = -1, next_hit = INT_MAX;
	std::string str;
	
	for ( ; start < end; ++start) {
	    int off;
	    
	    this->tc_sub_source->text_value_for_line(*this, start, str);
	    
	    for (off = 0; off < (int)str.size(); ) {
		int rc, matches[60];
		
		rc = pcre_exec(hl.h_code,
			       NULL,
			       str.c_str(),
			       str.size(),
			       off,
			       0,
			       matches,
			       60);
		if (rc > 0) {
		    struct line_range lr;
		    
		    if (rc == 2) {
			lr.lr_start = matches[2];
			lr.lr_end   = matches[3];
		    }
		    else {
			lr.lr_start = matches[0];
			lr.lr_end   = matches[1];
		    }

		    if (lr.lr_start < off_start) {
			prev_hit = std::max(prev_hit, lr.lr_start);
		    }
		    else if (lr.lr_start > off_start) {
			next_hit = std::min(next_hit, lr.lr_start);
		    }
		    if (lr.lr_end > lr.lr_start) {
			off = matches[1];
		    }
		    else {
			off += 1;
		    }
		}
		else {
		    off = str.size();
		}
	    }
	}

	range_out = std::make_pair(prev_hit, next_hit);
    };
    
    void set_search_action(action sa) { this->tc_search_action = sa; };

    template<class _Receiver>
    void set_search_action(action::mem_functor_t < _Receiver > *mf)
    {
	this->tc_search_action = action(mf);
    };

    void grep_end_batch(grep_proc &gp)
    {
	if (this->tc_follow_search && !this->tc_bookmarks[&BM_SEARCH].empty()) {
	    vis_line_t first_hit;

	    first_hit = this->tc_bookmarks[&BM_SEARCH].
			next(vis_line_t(this->get_top() - 1));
	    if (first_hit != -1) {
		if (first_hit > 0) {
		    --first_hit;
		}
		this->set_top(first_hit);
	    }
	}
	this->tc_search_action.invoke(this);
    };
    void grep_end(grep_proc &gp);

    size_t listview_rows(const listview_curses &lv)
    {
	return this->tc_sub_source == NULL ? 0 :
	       this->tc_sub_source->text_line_count();
    };
    
    void listview_value_for_row(const listview_curses &lv,
				vis_line_t line,
				attr_line_t &value_out);
    
    bool grep_value_for_line(int line, std::string &value_out)
    {
	bool retval = false;

	if (line < (int)this->tc_sub_source->text_line_count()) {
	    this->tc_sub_source->text_value_for_line(*this,
						     line,
						     value_out,
						     true);
	    retval = true;
	}

	return retval;
    };

    void grep_begin(grep_proc &gp);
    void grep_match(grep_proc &gp,
		    grep_line_t line,
		    int start,
		    int end);

    bool is_searching(void) { return this->tc_searching; };

    void set_follow_search(bool fs) { this->tc_follow_search = fs; };

    size_t get_match_count(void)
    {
	return this->tc_match_count;
    };

    void match_reset() {
	this->tc_match_count = 0;
	this->tc_bookmarks[&BM_SEARCH].clear();
    };

    typedef std::map<std::string, highlighter> highlight_map_t;

    highlight_map_t &get_highlights() { return this->tc_highlights; };

    void reload_data(void);

protected:
    text_sub_source *tc_sub_source;

    vis_bookmarks tc_bookmarks;

    vis_line_t tc_lview_top;
    int        tc_lview_left;

    int    tc_match_count;
    bool   tc_searching;
    bool   tc_follow_search;
    action tc_search_action;
    
    highlight_map_t           tc_highlights;
    highlight_map_t::iterator tc_current_highlight;
};

#endif
