
#include "config.h"

#include <algorithm>

#include "lnav_util.hh"
#include "logfile_sub_source.hh"


using namespace std;

#if 0
// XXX
class logfile_scrub_map {
public:

    static logfile_scrub_map &singleton()
    {
	static logfile_scrub_map s_lsm;

	return s_lsm;
    };

    const pcre *include(logfile::format_t format) const
    {
	map<logfile::format_t, pcre *>::const_iterator iter;
	pcre *retval = NULL;

	if ((iter = this->lsm_include.find(format)) != this->lsm_include.end()) {
	    retval = iter->second;
	}

	return retval;
    };

    const pcre *exclude(logfile::format_t format) const
    {
	map<logfile::format_t, pcre *>::const_iterator iter;
	pcre *retval = NULL;

	if ((iter = this->lsm_exclude.find(format)) != this->lsm_exclude.end()) {
	    retval = iter->second;
	}

	return retval;
    };

private:

    pcre *build_pcre(const char *pattern)
    {
	const char *errptr;
	pcre       *retval;
	int        eoff;

	retval = pcre_compile(pattern, PCRE_CASELESS, &errptr, &eoff, NULL);
	if (retval == NULL) {
	    throw errptr;
	}

	return retval;
    };

    logfile_scrub_map()
    {
	this->lsm_include[logfile::FORMAT_JBOSS] = this->
						   build_pcre("\\d+-(\\d+-\\d+ \\d+:\\d+:\\d+),\\d+ [^ ]+( .*)");
	this->lsm_exclude[logfile::FORMAT_JBOSS] = this->
						   build_pcre("(?:"
							      "\\[((?:[0-9a-zA-Z]+\\.)+)\\w+"
							      "|\\[[\\w: ]+ ((?:[0-9a-zA-Z]+\\.)+)\\w+[^ \\]]+"
							      "| ((?:[0-9a-zA-Z]+\\.)+)\\w+\\])");

	this->lsm_include[logfile::FORMAT_SYSLOG] = this->
						    build_pcre("(\\w+\\s[\\s\\d]\\d \\d+:\\d+:\\d+) \\w+( .*)");
    };

    map<logfile::format_t, pcre *> lsm_include;
    map<logfile::format_t, pcre *> lsm_exclude;
};
#endif

bookmark_type_t logfile_sub_source::BM_ERRORS;
bookmark_type_t logfile_sub_source::BM_WARNINGS;
bookmark_type_t logfile_sub_source::BM_FILES;

logfile_sub_source::logfile_sub_source()
    : lss_flags(0),
      lss_filtered_count(0)
{
}

logfile_sub_source::~logfile_sub_source()
{ }

logfile *logfile_sub_source::find(const char *fn,
				  content_line_t &line_base)
{
    std::   vector<logfile_data>::iterator iter;
    logfile *retval = NULL;

    line_base = content_line_t(0);
    for (iter = this->lss_files.begin();
	 iter != this->lss_files.end() && retval == NULL;
	 iter++) {
	if (strcmp(iter->ld_file->get_filename().c_str(), fn) == 0) {
	    retval = iter->ld_file;
	}
	else {
	    line_base += content_line_t(iter->ld_file->size());
	}
    }

    return retval;
}

vis_line_t logfile_sub_source::find_from_time(time_t start)
{
    vector<content_line_t>::iterator lb;
    vis_line_t retval(-1);

    lb = lower_bound(this->lss_index.begin(),
		     this->lss_index.end(),
		     start,
		     logline_cmp(*this));
    if (lb != this->lss_index.end()) {
	retval = vis_line_t(lb - this->lss_index.begin());
    }

    return retval;
}

void logfile_sub_source::text_value_for_line(textview_curses &tc,
					     int row,
					     string &value_out,
					     bool raw)
{
    content_line_t line(0);

    size_t tab;

    line = this->lss_index[row];
    this->lss_token_file   = this->find(line);
    this->lss_token_line   = this->lss_token_file->begin() + line;
    this->lss_token_offset = 0;
    this->lss_scrub_len    = 0;

    if (raw) {
	this->lss_token_file->read_line(this->lss_token_line, value_out);
	return;
    }

    this->lss_token_value =
	this->lss_token_file->read_line(this->lss_token_line);

    while ((tab = this->lss_token_value.find('\t')) != string::npos) {
	this->lss_token_value = this->lss_token_value.replace(tab, 1, 8, ' ');
    }

    this->lss_token_date_end = 0;
#if 0
    if (!(this->lss_flags & F_NO_SCRUB)) {
	log_format *format = this->lss_token_file->get_format();
	const pcre        *scrubber = NULL;
	int matches[60];
	int lpc, rc;

	// scrubber = logfile_scrub_map::singleton().include(format);
	if (scrubber != NULL) {
	    memset(matches, 0, sizeof(matches));
	    rc = pcre_exec(logfile_scrub_map::
			   singleton().include(format),
			   NULL,
			   this->lss_token_value.c_str(),
			   this->lss_token_value.size(),
			   0,
			   0,
			   matches,
			   60);
	    matches[1] = 0;
	    for (lpc = rc - 1; lpc >= 1; lpc--) {
		int c_end   = matches[lpc * 2];
		int c_start = matches[lpc * 2 - 1];

		this->lss_token_value.
		erase(this->lss_token_value.begin() + c_start,
		      this->lss_token_value.begin() + c_end);
	    }

	    if (rc > 1) {
		this->lss_token_date_end = 15;
	    }                                  /* XXX */
	}

	scrubber = logfile_scrub_map::singleton().exclude(format);
	if (scrubber != NULL) {
	    do {
		rc = pcre_exec(logfile_scrub_map::singleton().
			       exclude(format),
			       NULL,
			       this->lss_token_value.c_str(),
			       this->lss_token_value.size(),
			       0,
			       0,
			       matches,
			       60);
		for (lpc = rc - 1; lpc >= 1; lpc--) {
		    int c_start = matches[lpc * 2];
		    int c_end   = matches[lpc * 2 + 1];

		    if (c_start != -1 && c_end != -1) {
			this->lss_token_value.
			erase(this->lss_token_value.begin() + c_start,
			      this->lss_token_value.begin() + c_end);
		    }
		}
	    } while (rc > 0);
	}
    }
#endif

    value_out = this->lss_token_value;
}

void logfile_sub_source::text_attrs_for_line(textview_curses &lv,
					     int row,
					     string_attrs_t &value_out)
{
    view_colors &vc = view_colors::singleton();
    logline *next_line = NULL;
    struct line_range lr;
    int attrs = 0;
    
    switch (this->lss_token_line->get_level() & ~logline::LEVEL__FLAGS) {
    case logline::LEVEL_CRITICAL:
    case logline::LEVEL_ERROR:
	attrs = vc.attrs_for_role(view_colors::VCR_ERROR);
	break;

    case logline::LEVEL_WARNING:
	attrs = vc.attrs_for_role(view_colors::VCR_WARNING);
	break;

    default:
	attrs = vc.attrs_for_role(view_colors::VCR_TEXT);
	break;
    }

    if ((row + 1) < (int)this->lss_index.size()) {
	next_line = this->find_line(this->lss_index[row + 1]);
    }

    if (next_line != NULL &&
	(day_num(next_line->get_time()) >
	 day_num(this->lss_token_line->get_time()))) {
	attrs |= A_UNDERLINE;
    }

    lr.lr_start = this->lss_token_date_end;
    lr.lr_end = -1;
    
    value_out[lr].insert(make_string_attr("style", attrs));
    
    lr.lr_start = 0;
    lr.lr_end = -1;
    value_out[lr].insert(make_string_attr("file", this->lss_token_file));

    if (this->lss_token_date_end > 0 &&
	((this->lss_token_line->get_time() / (60 * 60)) % 2) == 0) {
	attrs = vc.attrs_for_role(view_colors::VCR_ALT_ROW);
	lr.lr_start = 0;
	lr.lr_end   = this->lss_token_date_end;
	
	value_out[lr].insert(make_string_attr("style", attrs));
    }
}

bool logfile_sub_source::rebuild_index(observer *obs, bool force)
{
    std::vector<logfile_data>::iterator iter;
    bool retval = force;

    for (iter = this->lss_files.begin();
	 iter != this->lss_files.end();
	 iter++) {
	if (iter->ld_file->rebuild_index(obs)) {
	    retval = true;
	}
	if (force) {
	    iter->ld_lines_indexed = 0;
	}
    }

    if (retval || force) {
	int    file_index = 0;
	string line_value;

	line_value.reserve(4 * 1024);
	if (force) {
	    this->lss_index.clear();
	    this->lss_filtered_count = 0;
	}

	for (iter = this->lss_files.begin();
	     iter != this->lss_files.end();
	     iter++, file_index++) {
	    if (iter->ld_lines_indexed < iter->ld_file->size()) {
		content_line_t con_line(file_index *MAX_LINES_PER_FILE +
					iter->ld_lines_indexed);

		logfile_filter::type_t action = logfile_filter::INCLUDE;
		content_line_t start_line(con_line + 1);

		logfile::iterator lf_iter;
		int action_priority = -1;

		this->lss_index.reserve(this->lss_index.size() +
					iter->ld_file->size() -
					iter->ld_lines_indexed);
		for (lf_iter = iter->ld_file->begin() + iter->ld_lines_indexed;
		     lf_iter != iter->ld_file->end();
		     lf_iter++) {
		    int lpc;

		    if (obs != NULL) {
			obs->logfile_sub_source_filtering(
			    *this,
			    content_line_t(con_line % MAX_LINES_PER_FILE),
			    iter->ld_file->size());
		    }

		    if (!(lf_iter->get_level() & logline::LEVEL_CONTINUED)) {
			if (action == logfile_filter::INCLUDE ||
			    action == logfile_filter::MAYBE) {
			    while (start_line < con_line) {
				this->lss_index.push_back(start_line);
				++start_line;
			    }
			}
			start_line      = con_line;
			action          = logfile_filter::MAYBE;
			action_priority = -1;
		    }

		    for (lpc = 0; lpc < (int)this->lss_filters.size(); lpc++) {
			logfile_filter *filter = this->lss_filters[lpc];

			if (filter->is_enabled()) {
			    bool matched;

			    if (line_value.empty()) {
				iter->ld_file->read_line(lf_iter, line_value);
			    }
			    matched = filter->matches(line_value);

			    switch (filter->get_type()) {
			    case logfile_filter::INCLUDE:
				if (lpc >= action_priority) {
				    if (matched) {
					action = logfile_filter::INCLUDE;
				    }
				    else if (action == logfile_filter::MAYBE) {
					action = logfile_filter::EXCLUDE;
				    }
				    action_priority = lpc;
				}
				break;

			    case logfile_filter::EXCLUDE:
				if (lpc >= action_priority) {
				    if (matched) {
					action = logfile_filter::EXCLUDE;
				    }
				    action_priority = lpc;
				}
				break;

			    default:
				assert(0);
				break;
			    }
			}
		    }
		    line_value.clear();

		    ++con_line;
		}

		if (action == logfile_filter::INCLUDE ||
		    action == logfile_filter::MAYBE) {
		    while (start_line < con_line) {
			this->lss_index.push_back(start_line);
			++start_line;
		    }
		}

		iter->ld_lines_indexed = iter->ld_file->size();

		assert(iter->ld_lines_indexed < MAX_LINES_PER_FILE);
	    }
	}

	stable_sort(this->lss_index.begin(),
		    this->lss_index.end(),
		    logline_cmp(*this));
    }

    return retval;
}

void logfile_sub_source::text_update_marks(bookmarks &bm)
{
    logfile    *last_file = NULL;
    vis_line_t vl;

    bm[&BM_WARNINGS].clear();
    bm[&BM_ERRORS].clear();
    bm[&BM_FILES].clear();
    bm[&textview_curses::BM_USER].clear();

    for (; vl < (int)this->lss_index.size(); ++vl) {
	content_line_t cl = this->lss_index[vl];
	logfile        *lf;

	if (binary_search(this->lss_user_marks.begin(),
			  this->lss_user_marks.end(),
			  cl)) {
	    bm[&textview_curses::BM_USER].insert_once(vl);
	}

	lf = this->find(cl);

	if (lf != last_file) {
	    bm[&BM_FILES].insert_once(vl);
	}

	switch ((*lf)[cl].get_level() & ~logline::LEVEL_MULTILINE) {
	case logline::LEVEL_WARNING:
	    bm[&BM_WARNINGS].insert_once(vl);
	    break;

	case logline::LEVEL_ERROR:
	case logline::LEVEL_CRITICAL:
	    bm[&BM_ERRORS].insert_once(vl);
	    break;

	default:
	    break;
	}

	last_file = lf;
    }
}

#if 0
void logfile_sub_source::handle_scroll(listview_curses *lc)
{
    printf("hello, world!\n");
    return;

    vis_line_t top = lc->get_top();

    if (this->lss_index.empty()) {
	this->lss_top_time    = -1;
	this->lss_bottom_time = -1;
    }
    else {
	time_t top_day, bottom_day, today, yesterday, now = time(NULL);
	vis_line_t bottom(0), height(0);

	unsigned long width;
	char          status[32];
	logline       *ll;

	today     = day_num(now);
	yesterday = today - 1;

	lc->get_dimensions(height, width);

	ll = this->find_line(this->lss_index[top]);
	this->lss_top_time = ll->get_time();

	bottom = min(top + height - vis_line_t(1),
		     vis_line_t(this->lss_index.size() - 1));

	ll = this->find_line(this->lss_index[bottom]);
	this->lss_bottom_time = ll->get_time();

	top_day    = day_num(this->lss_top_time);
	bottom_day = day_num(this->lss_bottom_time);
	if (top_day == today) {
	    snprintf(status, sizeof(status), "Today");
	}
	else if (top_day == yesterday) {
	    snprintf(status, sizeof(status), "Yesterday");
	}
	else {
	    strftime(status, sizeof(status),
		     "%a %b %d",
		     gmtime(&this->lss_top_time));
	}
	if (top_day != bottom_day) {
	    int len = strlen(status);

	    if (bottom_day == today) {
		snprintf(&status[len], sizeof(status) - len, " - Today");
	    }
	    else if (bottom_day == yesterday) {
		snprintf(&status[len], sizeof(status) - len, " - Yesterday");
	    }
	    else {
		strftime(&status[len], sizeof(status) - len,
			 " - %b %d",
			 gmtime(&this->lss_bottom_time));
	    }
	}

	this->lss_date_field.set_value(string(status));
    }
}

#endif
