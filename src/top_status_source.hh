
#ifndef _top_status_source_hh
#define _top_status_source_hh

#include <string>

#include "logfile_sub_source.hh"
#include "statusview_curses.hh"

class top_status_source
  : public status_data_source
{
  
public:
  
    typedef listview_curses::action::mem_functor_t<
	top_status_source> lv_functor_t;
    typedef textview_curses::action::mem_functor_t<
	top_status_source> tv_functor_t;
  
    typedef enum {
	TSF_TIME,
	TSF_WARNINGS,
	TSF_ERRORS,
	TSF_FORMAT,
	TSF_FILENAME,

	TSF__MAX
    } field_t;

    top_status_source()
	: marks_wire(*this, &top_status_source::update_marks),
	  filename_wire(*this, &top_status_source::update_filename) {
	this->tss_fields[TSF_TIME].set_width(24);
	this->tss_fields[TSF_WARNINGS].set_width(10);
	this->tss_fields[TSF_ERRORS].set_width(10);
	this->tss_fields[TSF_ERRORS].set_role(view_colors::VCR_ALERT_STATUS);
	this->tss_fields[TSF_FORMAT].set_width(15);
	this->tss_fields[TSF_FORMAT].right_justify(true);
	this->tss_fields[TSF_FILENAME].set_width(35); // XXX
	this->tss_fields[TSF_FILENAME].right_justify(true);
    };

    lv_functor_t marks_wire;
    lv_functor_t filename_wire;
  
    size_t statusview_fields(void) { return TSF__MAX; };
  
    status_field &statusview_value_for_field(int field) {
      return this->tss_fields[field];
    };

    void update_time(void) {
	status_field &sf = this->tss_fields[TSF_TIME];
	time_t current_time = time(NULL);
	char buffer[32];
	
	strftime(buffer, sizeof(buffer),
		 "%a %b %d %H:%M:%S %Z",
		 localtime(&current_time));
	sf.set_value(buffer);
    };
    
    void update_marks(listview_curses *lc) {
	textview_curses *tc = dynamic_cast<textview_curses *>(lc);
	status_field &sfw = this->tss_fields[TSF_WARNINGS];
	status_field &sfe = this->tss_fields[TSF_ERRORS];
	bookmarks &bm = tc->get_bookmarks();
	unsigned long width;
	vis_line_t height;

	tc->get_dimensions(height, width);
	if (bm.find(&logfile_sub_source::BM_WARNINGS) != bm.end()) {
	    bookmark_vector &bv = bm[&logfile_sub_source::BM_WARNINGS];
	    bookmark_vector::iterator iter;

	    iter = lower_bound(bv.begin(), bv.end(), tc->get_top());
	    sfw.set_value("%9dW", distance(bv.begin(), iter));
	}
	else {
	    sfw.clear();
	}
	
	if (bm.find(&logfile_sub_source::BM_ERRORS) != bm.end()) {
	    bookmark_vector &bv = bm[&logfile_sub_source::BM_ERRORS];
	    bookmark_vector::iterator iter;

	    iter = lower_bound(bv.begin(), bv.end(), tc->get_top());
	    sfe.set_value("%9dE", distance(bv.begin(), iter));
	}
	else {
	    sfe.clear();
	}
    };

    void update_filename(listview_curses *lc) {
	if (lc->get_inner_height() > 0) {
	    status_field &sf_format = this->tss_fields[TSF_FORMAT];
	    status_field &sf_filename = this->tss_fields[TSF_FILENAME];
	    struct line_range lr = { 0, -1 };
	    attrs_map_t::iterator iter;
	    string_attrs_t sa;
	    attr_line_t al;
	    
	    lc->get_data_source()->
		listview_value_for_row(*lc, lc->get_top(), al);
	    sa = al.get_attrs();
	    iter = sa[lr].find("file");
	    if (iter != sa[lr].end()) {
		logfile *lf = (logfile *)iter->second.sa_ptr;

		if (lf->get_format())
		   sf_format.set_value("(%s)",
				       lf->get_format()->get_name().c_str());
		else
		   sf_format.set_value("(unknown)");
		sf_filename.set_value(lf->get_filename());
	    }
	    else {
		sf_format.set_value("(unknown)");
		sf_filename.clear();
	    }
	}
    };

private:
    status_field tss_fields[TSF__MAX];
  
};

#endif
