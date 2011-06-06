#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <pthread.h>

#include <time.h>
#include <glob.h>

#include <curses.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <readline/readline.h>

#include <map>
#include <stack>
#include <vector>
#include <memory>
#include <sstream>
#include <algorithm>
#include <functional>

#include <sqlite3.h>

#include "help.hh"
#include "auto_temp_file.hh"
#include "logfile.hh"
#include "lnav_util.hh"
#include "listview_curses.hh"
#include "statusview_curses.hh"
#include "vt52_curses.hh"
#include "readline_curses.hh"
#include "textview_curses.hh"
#include "logfile_sub_source.hh"
#include "textfile_sub_source.hh"
#include "grep_proc.hh"
#include "bookmarks.hh"
#include "hist_source.hh"
#include "top_status_source.hh"
#include "bottom_status_source.hh"
#include "piper_proc.hh"
#include "log_vtab_impl.hh"
#include "db_sub_source.hh"
#include "pcrecpp.h"
#include "termios_guard.hh"

using namespace std;

typedef enum {
    LNM_PAGING,
    LNM_COMMAND,
    LNM_SEARCH,
    LNM_CAPTURE,
    LNM_SQL,
} ln_mode_t;

enum {
    LNB_SYSLOG,

    LNB__MAX,

    LNB_ROTATED
};

typedef enum {
    LNF_SYSLOG        = (1L << LNB_SYSLOG),

    LNF_ROTATED       = (1L << LNB_ROTATED),

    LNF__ALL          = (LNF_SYSLOG)
} lnav_flags_t;

static multimap<lnav_flags_t, string> DEFAULT_FILES;

typedef enum {
    LNV_LOG,
    LNV_TEXT,
    LNV_HELP,
    LNV_HISTOGRAM,
    LNV_GRAPH,
    LNV_DB,

    LNV__MAX
} lnav_view_t;

typedef enum {
    LNS_TOP,
    LNS_BOTTOM,

    LNS__MAX
} lnav_status_t;

typedef enum {
    LG_GRAPH,
    LG_CAPTURE,
    
    LG__MAX
} lnav_grep_t;

struct hist_level {
    int hl_bucket_size;
    int hl_group_size;
};

static struct hist_level HIST_ZOOM_VALUES[] = {
    { 24 * 60 * 60, 7 * 24 * 60 * 60 },
    {  4 * 60 * 60,     24 * 60 * 60 },
    {      60 * 60,     24 * 60 * 60 },
    {      10 * 60,          60 * 60 },
    {           60,          60 * 60 },
};

static const int HIST_ZOOM_LEVELS = sizeof(HIST_ZOOM_VALUES) / sizeof(struct hist_level);

class grep_highlighter {
public:
    grep_highlighter(auto_ptr < grep_proc > gp,
		     string hl_name,
		     textview_curses::highlight_map_t &hl_map)
	: gh_code(gp->get_code()),
	  gh_grep_proc(gp),
	  gh_hl_name(hl_name),
	  gh_hl_map(hl_map) { };

    ~grep_highlighter()
    {
	this->gh_hl_map.erase(this->gh_hl_map.find(this->gh_hl_name));
    };

    grep_proc *get_grep_proc() { return this->gh_grep_proc.get(); };

private:
    auto_mem<pcre> gh_code;
    auto_ptr<grep_proc> gh_grep_proc;
    string gh_hl_name;
    textview_curses::highlight_map_t gh_hl_map;
};

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
				   string &label_out)
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
		label_out += string(buffer);
	    }
	};
    };

    label_source gr_label_source;
    textview_curses::highlighter *gr_highlighter;
    vector<grep_line_t> gr_lines;
    int           gr_x;
    bucket_type_t gr_next_field;
};

/* XXX figure out how to do this with the template */
void sqlite_close_wrapper(void *mem)
{
    sqlite3_close((sqlite3*)mem);
}

static struct {
    const char           *ld_program_name;
    const char *ld_debug_log_name;

    set< pair<string, int> > ld_file_names;
    sig_atomic_t         ld_looping;
    sig_atomic_t         ld_winched;
    unsigned long        ld_flags;
    WINDOW               *ld_window;
    ln_mode_t            ld_mode;

    statusview_curses    ld_status[LNS__MAX];
    top_status_source    ld_top_source;
    bottom_status_source ld_bottom_source;
    listview_curses::action::broadcaster ld_scroll_broadcaster;

    time_t               ld_top_time;
    time_t               ld_bottom_time;

    stack<textview_curses *> ld_view_stack;
    textview_curses      ld_views[LNV__MAX];
    auto_ptr<grep_highlighter> ld_search_child[LNV__MAX];
    vis_line_t ld_search_start_line;
    readline_curses      *ld_rl_view;

    logfile_sub_source   ld_log_source;
    hist_source          ld_hist_source;
    int                  ld_hist_zoom;

    textfile_sub_source  ld_text_source;

    map<textview_curses *, int> ld_last_user_mark;

    grapher              ld_graph_source;

    hist_source ld_db_source;
    db_label_source ld_db_rows;

    int                  ld_max_fd;
    fd_set               ld_read_fds;

    auto_ptr<grep_highlighter> ld_grep_child[LG__MAX];

    log_vtab_manager *ld_vtab_manager;
    auto_mem<sqlite3, sqlite_close_wrapper> ld_db;
} lnav_data;

class loading_observer
    : public logfile_sub_source::observer {
public:
    loading_observer()
	: lo_last_offset(0),
	  lo_last_line(0) { };

    void logfile_indexing(logfile &lf, off_t off, size_t total)
    {
	// XXX assert(off <= total);
	if (off > total)
	    off = total;
	
	if (abs(off - this->lo_last_offset) > (128 * 1024) ||
	    (size_t)off == total) {
	    lnav_data.ld_bottom_source.update_loading(off, total);
	    this->do_update();
	    this->lo_last_offset = off;
	}

	if (!lnav_data.ld_looping) {
	    throw logfile::error(lf.get_filename(), EINTR);
	}
    };

    void logfile_sub_source_filtering(logfile_sub_source &lss,
				      content_line_t cl,
				      size_t total)
    {
	if (abs(cl - this->lo_last_line) > 1024 || (size_t)cl == (total - 1)) {
	    lnav_data.ld_bottom_source.update_loading(cl, (total - 1));
	    this->do_update();
	    this->lo_last_line = cl;
	}

	if (!lnav_data.ld_looping) {
	    throw logfile::error("", EINTR);
	}
    };

private:
    void do_update(void)
    {
	lnav_data.ld_top_source.update_time();
	lnav_data.ld_status[LNS_TOP].do_update();
	lnav_data.ld_status[LNS_BOTTOM].do_update();
	refresh();
    };

    off_t          lo_last_offset;
    content_line_t lo_last_line;
};

static void rebuild_hist(size_t old_count, bool force)
{
    textview_curses &hist_view = lnav_data.ld_views[LNV_HISTOGRAM];
    logfile_sub_source &lss    = lnav_data.ld_log_source;
    size_t new_count = lss.text_line_count();
    hist_source &hs = lnav_data.ld_hist_source;
    int    zoom_level = lnav_data.ld_hist_zoom;
    time_t old_time;
    int    lpc;

    old_time = hs.value_for_row(hist_view.get_top());
    hs.set_bucket_size(HIST_ZOOM_VALUES[zoom_level].hl_bucket_size);
    hs.set_group_size(HIST_ZOOM_VALUES[zoom_level].hl_group_size);
    if (force) {
	hs.clear();
    }
    for (lpc = old_count; lpc < (int)new_count; lpc++) {
	logline *ll = lss.find_line(lss.at(vis_line_t(lpc)));

	if (!(ll->get_level() & logline::LEVEL_CONTINUED)) {
	    hs.add_value(ll->get_time(),
			 bucket_type_t(ll->get_level() &
				       ~logline::LEVEL__FLAGS));
	}
    }
    hs.analyze();
    hist_view.reload_data();
    hist_view.set_top(hs.row_for_value(old_time));
}

static void rebuild_indexes(bool force)
{
    static loading_observer obs;

    logfile_sub_source &lss   = lnav_data.ld_log_source;
    textview_curses &log_view = lnav_data.ld_views[LNV_LOG];
    textview_curses &text_view = lnav_data.ld_views[LNV_TEXT];
    vis_line_t old_bottom(0), height(0);

    unsigned long width;
    bool          scroll_down;
    size_t        old_count;
    time_t        old_time;

    {
	textfile_sub_source *tss = &lnav_data.ld_text_source;
	std::list<logfile *>::iterator iter;
	size_t new_count;

	text_view.get_dimensions(height, width);
	old_bottom = text_view.get_top() + height;
	scroll_down = (size_t)old_bottom > tss->text_line_count();
	
	for (iter = tss->tss_files.begin();
	     iter != tss->tss_files.end();) {
	    (*iter)->rebuild_index(&obs);
	    if ((*iter)->get_format() != NULL) {
		logfile *lf = *iter;

		iter = tss->tss_files.erase(iter);
		lnav_data.ld_log_source.insert_file(lf);
		force = true;
	    }
	    else {
		++iter;
	    }
	}

	text_view.reload_data();

	new_count = tss->text_line_count();
	if (scroll_down && new_count >= old_bottom) {
	    text_view.set_top(vis_line_t(new_count - height + 1));
	}
    }

    old_time = lnav_data.ld_top_time;
    log_view.get_dimensions(height, width);
    old_bottom  = log_view.get_top() + height;
    scroll_down = (size_t)old_bottom > lss.text_line_count();
    if (force) {
	old_count = 0;
    }
    else {
	old_count = lss.text_line_count();
    }
    if (lss.rebuild_index(&obs, force)) {
	size_t      new_count = lss.text_line_count();
	grep_line_t start_line;
	int         lpc;

	log_view.reload_data();
	
	if (scroll_down && new_count >= old_bottom) {
	    log_view.set_top(vis_line_t(new_count - height + 1));
	}
	else if (!scroll_down && force) {
	    log_view.set_top(lss.find_from_time(old_time));
	}

	rebuild_hist(old_count, force);

	start_line = force ? grep_line_t(0) : grep_line_t(-1);

	if (force) {
		log_view.match_reset();
	}

	for (lpc = 0; lpc < LG__MAX; lpc++) {
	    if (lnav_data.ld_grep_child[lpc].get() != NULL) {
		lnav_data.ld_grep_child[lpc]->get_grep_proc()->
		    queue_request(start_line);
		lnav_data.ld_grep_child[lpc]->get_grep_proc()->start();
	    }
	}
	if (lnav_data.ld_search_child[LNV_LOG].get() != NULL) {
	    lnav_data.ld_search_child[LNV_LOG]->get_grep_proc()->reset();
	    lnav_data.ld_search_child[LNV_LOG]->get_grep_proc()->
		queue_request(start_line);
	    lnav_data.ld_search_child[LNV_LOG]->get_grep_proc()->start();
	}
    }

    lnav_data.ld_bottom_source.update_filtered(lss);
    lnav_data.ld_scroll_broadcaster.invoke(lnav_data.ld_view_stack.top());
}

class plain_text_source
    : public text_sub_source {
public:
    plain_text_source(string text)
    {
	size_t start = 0, end;

	while ((end = text.find('\n', start)) != string::npos) {
	    this->tds_lines.push_back(text.substr(start, end - start));
	    start = end + 1;
	}
	this->tds_lines.push_back(text.substr(start));
    };

    size_t text_line_count()
    {
	return this->tds_lines.size();
    };

    void text_value_for_line(textview_curses &tc,
			     int row,
			     string &value_out,
			     bool no_scrub)
    {
	value_out = this->tds_lines[row];
    };

private:
    vector<string> tds_lines;
};

class time_label_source
    : public hist_source::label_source {
public:
    time_label_source() { };

    void hist_label_for_bucket(int bucket_start_value,
			       const hist_source::bucket_t &bucket,
			       string &label_out)
    {
	hist_source::bucket_t::const_iterator iter;
	int  total = 0, errors = 0, warnings = 0;
	time_t bucket_time = bucket_start_value;
	struct tm *bucket_tm;
	char buffer[128];
	int  len;

	bucket_tm = gmtime((time_t *)&bucket_time);
	if (bucket_tm)
	    strftime(buffer, sizeof(buffer),
		     " %a %b %d %H:%M  ",
		     bucket_tm);
	else {
	    fprintf(stderr, "bad time %d\n", bucket_start_value);
	    buffer[0] = '\0';
	}
	for (iter = bucket.begin(); iter != bucket.end(); iter++) {
	    total += (int)iter->second;
	    switch (iter->first) {
	    case logline::LEVEL_ERROR:
	    case logline::LEVEL_CRITICAL:
		errors += (int)iter->second;
		break;

	    case logline::LEVEL_WARNING:
		warnings += (int)iter->second;
		break;
	    }
	}

	len = strlen(buffer);
	snprintf(&buffer[len], sizeof(buffer) - len,
		 " %8d total  %8d errors  %8d warnings",
		 total, errors, warnings);

	label_out = string(buffer);
    };
};

static string get_current_dir(void)
{
    char   cwd[FILENAME_MAX];
    string retval = ".";

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
	perror("getcwd");
    }
    else {
	retval = string(cwd);
    }

    if (retval != "/") {
      retval += "/";
    }

    return retval;
}

static bool change_to_parent_dir(void)
{
    bool retval = false;
    char cwd[3] = "";

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
	// perror("getcwd");
    }
    if (strcmp(cwd, "/") != 0) {
	if (chdir("..") == -1) {
	    perror("chdir('..')");
	}
	else {
	    retval = true;
	}
    }

    return retval;
}

static bool append_default_files(lnav_flags_t flag)
{
    bool retval = true;

    if (lnav_data.ld_flags & flag) {
	pair<multimap<lnav_flags_t, string>::iterator,
	     multimap<lnav_flags_t, string>::iterator> range;
	bool found = false;

	for (range = DEFAULT_FILES.equal_range(flag);
	     range.first != range.second && !found;
	     range.first++) {
	    string      path = range.first->second;
	    struct stat st;

	    if (access(path.c_str(), R_OK) == 0) {
		path = get_current_dir() + range.first->second;

		if (lnav_data.ld_flags & LNF_ROTATED) {
		    glob_t gl;

		    memset(&gl, 0, sizeof(gl));
		    path += "*";
		    if (glob(path.c_str(), 0, NULL, &gl) == 0) {
			int lpc;

			for (lpc = 0; lpc < (int)gl.gl_pathc; lpc++) {
			    lnav_data.ld_file_names.insert(make_pair(gl.gl_pathv[lpc], -1));
			}
			globfree(&gl);
		    }
		}
		else {
		    lnav_data.ld_file_names.insert(make_pair(path, -1));
		}
		found = true;
	    }
	    else if (stat(path.c_str(), &st) == 0) {
		fprintf(stderr,
			"error: cannot read -- %s%s\n",
			get_current_dir().c_str(),
			path.c_str());
		retval = false;
	    }
	}
    }

    return retval;
}

static void sigint(int sig)
{
    lnav_data.ld_looping = false;
}

static void sigwinch(int sig)
{
    lnav_data.ld_winched = true;
}


static void back_ten(int ten_minute)
{
    logfile_sub_source &lss = lnav_data.ld_log_source;

    time_t hour = rounddown_offset(lnav_data.ld_top_time,
				   60 * 60,
				   ten_minute * 10 * 60);
    vis_line_t line = lss.find_from_time(hour);

    --line;
    lnav_data.ld_view_stack.top()->set_top(line);
}

static bool toggle_view(textview_curses *toggle_tc)
{
    textview_curses *tc    = lnav_data.ld_view_stack.top();
    bool            retval = false;

    if (tc == toggle_tc) {
	lnav_data.ld_view_stack.pop();
    }
    else {
	lnav_data.ld_view_stack.push(toggle_tc);
	retval = true;
    }
    tc = lnav_data.ld_view_stack.top();
    tc->set_needs_update();
    lnav_data.ld_scroll_broadcaster.invoke(tc);

    return retval;
}

static void ensure_view(textview_curses *expected_tc)
{
    textview_curses *tc    = lnav_data.ld_view_stack.top();

    if (tc != expected_tc) {
	toggle_view(expected_tc);
    }
}

static void moveto_cluster(vis_line_t (bookmark_vector::*f)(vis_line_t),
			   bookmark_type_t *bt,
			   vis_line_t top)
{
    textview_curses *tc = lnav_data.ld_view_stack.top();

    if (tc != &lnav_data.ld_views[LNV_LOG]) {
	flash();
    }
    else {
	logfile_sub_source &lss = lnav_data.ld_log_source;
	bookmarks &bm           = tc->get_bookmarks();
	vis_line_t vl(-1), last_top(top);

	logline::level_t last_level;
	bool             done = false;
	time_t           last_time;
	logline          *ll;

	ll         = lss.find_line(lss.at(top));
	last_time  = ll->get_time();
	last_level = ll->get_level();
	while (vl == -1 && (top = (bm[bt].*f)(top)) != -1) {
	    ll = lss.find_line(lss.at(top));
	    if (abs(last_top - top) > 1 ||
		ll->get_level() != last_level ||
		ll->get_time() != last_time) {
		last_time  = ll->get_time();
		last_level = ll->get_level();
		vl         = top;
	    }
	    last_top = top;
	}
	while (vl > 0 && !done) {
	    ll = lss.find_line(lss.at(vis_line_t(vl - 1)));
	    if (ll->get_level() != last_level || ll->get_time() != last_time) {
		done = true;
	    }
	    else {
		--vl;
	    }
	}
	tc->set_top(vl);
    }
}

/* XXX For one, this code is kinda crappy.  For two, we should probably link
 * directly with X so we don't need to have xclip installed and it'll work if
 * we're ssh'd into a box.
 */
static void copy_to_xclip(void)
{
    textview_curses *tc = lnav_data.ld_view_stack.top();
    bookmark_vector &bv = tc->get_bookmarks()[&textview_curses::BM_USER];
    bookmark_vector::iterator iter;
    FILE *pfile = NULL;
    string line;
    
    if ((pfile = popen("xclip -i > /dev/null", "w")) == NULL) {
	flash();
	return;
    }

    for (iter = bv.begin(); iter != bv.end(); iter++) {
	tc->grep_value_for_line(*iter, line);
	fprintf(pfile, "%s\n", line.c_str());
    }

    fclose(pfile);
    pfile = NULL;
}

static void handle_paging_key(int ch)
{
    textview_curses *tc = lnav_data.ld_view_stack.top();

    logfile_sub_source &lss = lnav_data.ld_log_source;
    bookmarks &bm           = tc->get_bookmarks();

    if (tc->handle_key(ch)) {
	return;
    }

    /* process the command keystroke */
    switch (ch) {
    case 'q':
    case 'Q':
	lnav_data.ld_view_stack.pop();
	if (lnav_data.ld_view_stack.empty() ||
	    (lnav_data.ld_view_stack.size() == 1 &&
	     lnav_data.ld_log_source.text_line_count() == 0)) {
	    lnav_data.ld_looping = false;
	}
	else {
	    tc = lnav_data.ld_view_stack.top();
	    tc->set_needs_update();
	}
	break;

    case 'c':
	copy_to_xclip();
	break;

    case 'e':
	moveto_cluster(&bookmark_vector::next,
		       &logfile_sub_source::BM_ERRORS,
		       tc->get_top());
	break;

    case 'E':
	moveto_cluster(&bookmark_vector::prev,
		       &logfile_sub_source::BM_ERRORS,
		       tc->get_top());
	break;

    case 'w':
	moveto_cluster(&bookmark_vector::next,
		       &logfile_sub_source::BM_WARNINGS,
		       tc->get_top());
	break;

    case 'W':
	moveto_cluster(&bookmark_vector::prev,
		       &logfile_sub_source::BM_WARNINGS,
		       tc->get_top());
	break;

    case 'n':
	tc->set_top(bm[&textview_curses::BM_SEARCH].next(tc->get_top()));
	break;

    case 'N':
	tc->set_top(bm[&textview_curses::BM_SEARCH].prev(tc->get_top()));
	break;

    case '>':
	{
	    std::pair<int, int> range;

	    tc->horiz_shift(tc->get_top(),
			    tc->get_bottom(),
			    tc->get_left(),
			    "(search",
			    range);
	    if (range.second != INT_MAX)
		tc->set_left(range.second);
	    else
		flash();
	}
	break;
    case '<':
	if (tc->get_left() == 0) {
	    flash();
	}
	else {
	    std::pair<int, int> range;

	    tc->horiz_shift(tc->get_top(),
			    tc->get_bottom(),
			    tc->get_left(),
			    "(search",
			    range);
	    if (range.first != -1)
		tc->set_left(range.first);
	    else
		tc->set_left(0);
	}
	break;

    case 'f':
	if (tc == &lnav_data.ld_views[LNV_LOG]) {
	    tc->set_top(bm[&logfile_sub_source::BM_FILES].next(tc->get_top()));
	}
	else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
	    textfile_sub_source &tss = lnav_data.ld_text_source;

	    if (!tss.tss_files.empty()) {
		tss.tss_files.push_front(tss.tss_files.back());
		tss.tss_files.pop_back();
		tc->reload_data();
	    }
	}
	break;

    case 'F':
	if (tc == &lnav_data.ld_views[LNV_LOG]) {
	    tc->set_top(bm[&logfile_sub_source::BM_FILES].prev(tc->get_top()));
	}
	else if (tc == &lnav_data.ld_views[LNV_TEXT]) {
	    textfile_sub_source &tss = lnav_data.ld_text_source;
	    
	    if (!tss.tss_files.empty()) {
		tss.tss_files.push_back(tss.tss_files.front());
		tss.tss_files.pop_front();
		tc->reload_data();
	    }
	}
	break;

#if 0
	/* XXX superceded by 'I' ? */
    case 'j':
	if (tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
	    time_t top_time;

	    top_time = lnav_data.ld_hist_source.value_for_row(tc->get_top());

	    do {
		lnav_data.ld_view_stack.pop();
		tc = lnav_data.ld_view_stack.top();
	    } while (tc != &lnav_data.ld_views[LNV_LOG]);
	    tc->set_top(lss.find_from_time(top_time));
	    tc->set_needs_update();
	}
	else if (tc == &lnav_data.ld_views[LNV_GRAPH]) {
	    grapher &gr = lnav_data.ld_graph_source;
	    int row = gr.value_for_row(tc->get_top());

	    do {
		lnav_data.ld_view_stack.pop();
		tc = lnav_data.ld_view_stack.top();
	    } while (tc != &lnav_data.ld_views[LNV_LOG]);
	    tc->set_top(vis_line_t(gr.at(row)));
	    tc->set_needs_update();
	}
	break;
#endif

    case 'z':
	if (tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
	    if ((lnav_data.ld_hist_zoom + 1) >= HIST_ZOOM_LEVELS) {
		flash();
	    }
	    else {
		lnav_data.ld_hist_zoom += 1;
		rebuild_hist(0, true);
	    }
	}
	break;

    case 'Z':
	if (tc == &lnav_data.ld_views[LNV_HISTOGRAM]) {
	    if (lnav_data.ld_hist_zoom == 0) {
		flash();
	    }
	    else {
		lnav_data.ld_hist_zoom -= 1;
		rebuild_hist(0, true);
	    }
	}
	break;

    case 'u':
	tc->set_top(tc->get_bookmarks()[&textview_curses::BM_USER].
		    next(tc->get_top()));
	break;

    case 'U':
	tc->set_top(tc->get_bookmarks()[&textview_curses::BM_USER].
		    prev(tc->get_top()));
	break;

    case 'm':
	lnav_data.ld_last_user_mark[tc] = tc->get_top();
	tc->toggle_user_mark(lnav_data.ld_last_user_mark[tc]);
	tc->reload_data();
	break;
    case 'J':
	// TODO: if they scroll up, we should start marking again from the top.
	// We should also scroll down as the continue to mark stuff.  If they
	// move somewhere else in the file, we should also start marking from
	// the top again.
	if (lnav_data.ld_last_user_mark.find(tc) == lnav_data.ld_last_user_mark.end()) {
	    lnav_data.ld_last_user_mark[tc] = tc->get_top();
	}
	else if (lnav_data.ld_last_user_mark[tc] + 1 > tc->get_bottom()) {
	    flash();
	    break; // XXX
	}
	else {
	    lnav_data.ld_last_user_mark[tc] += 1;
	}
	tc->toggle_user_mark(lnav_data.ld_last_user_mark[tc]);
	tc->reload_data();
	break;
    case 'K':
	// TODO: scroll up with the selection
	if (lnav_data.ld_last_user_mark.find(tc) == lnav_data.ld_last_user_mark.end()) {
	    lnav_data.ld_last_user_mark[tc] = tc->get_top();
	}

	tc->toggle_user_mark(lnav_data.ld_last_user_mark[tc]);
	if (lnav_data.ld_last_user_mark[tc] - 1 < 0) {
	    flash();
	}
	else {
	    lnav_data.ld_last_user_mark[tc] -= 1;
	}
	tc->reload_data();
	break;
    case 'M':
	if (lnav_data.ld_last_user_mark.find(tc) == lnav_data.ld_last_user_mark.end()) {
	    flash();
	}
	else {
	    int start_line = min((int)tc->get_top(), lnav_data.ld_last_user_mark[tc] + 1);
	    int end_line = max((int)tc->get_top(), lnav_data.ld_last_user_mark[tc] - 1);
	    
	    tc->toggle_user_mark(start_line, end_line);
	    tc->reload_data();
	}
	break;

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
	{
	    int    ten_minute = (ch - '0') * 10 * 60;
	    time_t hour       = rounddown(lnav_data.ld_top_time +
					  (60 * 60) -
					  ten_minute +
					  1,
					  60 * 60);
	    vis_line_t line = lss.find_from_time(hour + ten_minute);

	    tc->set_top(line);
	}
	break;

    case '!':
	back_ten(1);
	break;

    case '@':
	back_ten(2);
	break;

    case '#':
	back_ten(3);
	break;

    case '$':
	back_ten(4);
	break;

    case '%':
	back_ten(5);
	break;

    case '^':
	back_ten(6);
	break;

    case '0':
	{
	    time_t     first_time = lnav_data.ld_top_time;
	    int        step       = 24 * 60 * 60;
	    vis_line_t line       = lss.find_from_time(roundup(first_time, step));

	    tc->set_top(line);
	}
	break;

    case ')':
	{
	    time_t     day  = rounddown(lnav_data.ld_top_time, 24 * 60 * 60);
	    vis_line_t line = lss.find_from_time(day);

	    --line;
	    tc->set_top(line);
	}
	break;

    case 'D':
    case 'O':
	if (tc->get_top() == 0) {
	    flash();
	}
	else {
	    int        step     = ch == 'D' ? (24 * 60 * 60) : (60 * 60);
	    time_t     top_time = lnav_data.ld_top_time;
	    vis_line_t line     = lss.find_from_time(top_time - step);

	    if (line != 0) {
		--line;
	    }
	    tc->set_top(line);
	}
	break;

    case 'd':
    case 'o':
	{
	    int        step = ch == 'd' ? (24 * 60 * 60) : (60 * 60);
	    vis_line_t line = lss.find_from_time(lnav_data.ld_top_time + step);

	    tc->set_top(line);
	}
	break;

    case 's':
	lnav_data.ld_log_source.toggle_scrub();
	tc->reload_data();
	break;

    case ':':
	lnav_data.ld_mode = LNM_COMMAND;
	lnav_data.ld_rl_view->focus(LNM_COMMAND, ":");
	break;

    case '/':
	lnav_data.ld_mode = LNM_SEARCH;
	lnav_data.ld_search_start_line = lnav_data.ld_view_stack.top()->
	    get_top();
	lnav_data.ld_rl_view->focus(LNM_SEARCH, "/");
	break;

    case ';':
	lnav_data.ld_mode = LNM_SQL;
	lnav_data.ld_rl_view->focus(LNM_SQL, ";");
	break;

    case 't':
	toggle_view(&lnav_data.ld_views[LNV_TEXT]);
	break;

    case 'i':
	toggle_view(&lnav_data.ld_views[LNV_HISTOGRAM]);
	break;

    case 'I':
	{
	    time_t log_top = lnav_data.ld_top_time;
	    
	    time_t hist_top =
		lnav_data.ld_hist_source.value_for_row(tc->get_top());

	    if (toggle_view(&lnav_data.ld_views[LNV_HISTOGRAM])) {
		hist_source &hs = lnav_data.ld_hist_source;

		tc = lnav_data.ld_view_stack.top();
		tc->set_top(hs.row_for_value(log_top));
	    }
	    else {
		tc = &lnav_data.ld_views[LNV_LOG];
		tc->set_top(lss.find_from_time(hist_top));
		tc->set_needs_update();
	    }
	}
	break;

    case 'g':
	toggle_view(&lnav_data.ld_views[LNV_GRAPH]);
	break;

    case '?':
	toggle_view(&lnav_data.ld_views[LNV_HELP]);
	break;

    case 'v':
	toggle_view(&lnav_data.ld_views[LNV_DB]);
	break;
    case 'V':
	{
	    textview_curses *db_tc = &lnav_data.ld_views[LNV_DB];
	    db_label_source &dls = lnav_data.ld_db_rows;
	    hist_source &hs = lnav_data.ld_db_source;
	    
	    if (toggle_view(db_tc)) {
		int lpc;
		
		for (lpc = 0; lpc < dls.dls_headers.size(); lpc++) {
		    if (dls.dls_headers[lpc] != "line_number")
			continue;

		    char linestr[64];
		    int line_number = (int)tc->get_top();
		    int row;
		    
		    snprintf(linestr, sizeof(linestr), "%d", line_number);
		    for (row = 0; row < dls.dls_rows.size(); row++) {
			if (strcmp(dls.dls_rows[row][lpc].c_str(),
				   linestr) == 0) {
			    vis_line_t db_line(hs.row_for_value(row));

			    db_tc->set_top(db_line);
			    db_tc->set_needs_update();
			    break;
			}
		    }
		    break;
		}
	    }
	    else {
		int db_row = hs.value_for_row(db_tc->get_top());
		int lpc;

		for (lpc = 0; lpc < dls.dls_headers.size(); lpc++) {
		    if (dls.dls_headers[lpc] != "line_number")
			continue;

		    int line_number;
		    
		    if (sscanf(dls.dls_rows[db_row][lpc].c_str(),
			       "%d",
			       &line_number) &&
			line_number >= 0 &&
			line_number < tc->listview_rows(*tc)) {
			tc = &lnav_data.ld_views[LNV_LOG];
			tc->set_top(vis_line_t(line_number));
			tc->set_needs_update();
		    }
		    break;
		}
	    }
	}
	break;

    default:
	// fprintf(stderr, "unhandled %d\n", ch);
	flash();
	break;
    }
}

static void handle_rl_key(int ch)
{
    switch (ch) {
    case KEY_PPAGE:
    case KEY_NPAGE:
	handle_paging_key(ch);
	break;

    default:
	lnav_data.ld_rl_view->handle_key(ch);
	break;
    }
}

static string com_unix_time(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a unix time value";

    if (args.size() == 0) { }
    else if (args.size() >= 2) {
	char      ftime[128] = "";
	bool      parsed     = false;
	struct tm log_time;
	time_t    u_time;
	size_t    millis;
	char      *rest;

	u_time   = time(NULL);
	log_time = *localtime( &u_time);

	log_time.tm_isdst = -1;

	args[1] = cmdline.substr(cmdline.find(args[1]));
	if ((millis = args[1].find('.')) != string::npos ||
	    (millis = args[1].find(',')) != string::npos) {
	    args[1] = args[1].erase(millis, 4);
	}
	if (((rest = strptime(args[1].c_str(),
			      "%b %d %H:%M:%S %Y",
			      &log_time)) != NULL &&
	     (rest - args[1].c_str()) >= 20) ||
	    ((rest = strptime(args[1].c_str(),
			      "%Y-%m-%d %H:%M:%S",
			      &log_time)) != NULL &&
	     (rest - args[1].c_str()) >= 19)) {
	    u_time = mktime(&log_time);
	    parsed = true;
	}
	else if (sscanf(args[1].c_str(), "%ld", &u_time)) {
	    log_time = *localtime( &u_time);

	    parsed = true;
	}
	if (parsed) {
	    int len;

	    strftime(ftime, sizeof(ftime),
		     "%a %b %d %H:%M:%S %Y  %z %Z",
		     localtime(&u_time));
	    len = strlen(ftime);
	    snprintf(ftime + len, sizeof(ftime) - len,
		     " -- %ld\n",
		     u_time);
	    retval = string(ftime);
	}
    }

    return retval;
}

static string com_current_time(string cmdline, vector<string> &args)
{
    char   ftime[128];
    string retval;
    time_t u_time;
    int    len;

    u_time = time(NULL);
    strftime(ftime, sizeof(ftime),
	     "%a %b %d %H:%M:%S %Y  %z %Z",
	     localtime(&u_time));
    len = strlen(ftime);
    snprintf(ftime + len, sizeof(ftime) - len,
	     " -- %ld\n",
	     u_time);
    retval = string(ftime);

    return retval;
}

static string com_goto(string cmdline, vector<string> &args)
{
    string retval = "error: expecting line number/percentage";

    if (args.size() == 0) { }
    else if (args.size() > 1) {
	textview_curses *tc = lnav_data.ld_view_stack.top();
	int             line_number, consumed;
	float           value;

	if (sscanf(args[1].c_str(), "%f%n", &value, &consumed) == 1) {
	    if (args[1][consumed] == '%') {
		line_number = (int)
			      ((double)tc->get_inner_height() * (value / 100.0));
	    }
	    else {
		line_number = (int)value;
	    }
	    tc->set_top(vis_line_t(line_number));

	    retval = "";
	}
    }

    return retval;
}

static string com_save_to(string cmdline, vector<string> &args)
{
    FILE *outfile = NULL;
    FILE *pfile = NULL;
    char command[1024];
    const char *mode = "";

    if (args.size() == 0) {
	args.push_back("filename");
	return "";
    }
    
    if (args.size() != 2) {
	return "error: expecting file name";
    }

    snprintf(command, sizeof(command), "echo -n %s", args[1].c_str());
    if ((pfile = popen(command, "r")) == NULL) {
	return "error: unable to compute file name";
    }

    if (fgets(command, sizeof(command), pfile) == 0)
	perror("fgets");
    fclose(pfile);
    pfile = NULL;

    if (args[0] == "append-to") {
	mode = "a";
    }
    else if (args[0] == "write-to") {
	mode = "w";
    }

    if ((outfile = fopen(command, mode)) == NULL) {
	return "error: unable to open file -- " + string(command);
    }
    
    textview_curses *tc = lnav_data.ld_view_stack.top();
    bookmark_vector &bv = tc->get_bookmarks()[&textview_curses::BM_USER];
    bookmark_vector::iterator iter;
    string line;
    
    for (iter = bv.begin(); iter != bv.end(); iter++) {
	tc->grep_value_for_line(*iter, line);
	fprintf(outfile, "%s\n", line.c_str());
    }

    fclose(outfile);
    outfile = NULL;

    return "";
}

static string com_highlight(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to highlight";

    if (args.size() == 0) { }
    else if (args.size() > 1) {
	const char *errptr;
	pcre       *code;
	int        eoff;

	args[1] = cmdline.substr(cmdline.find(args[1]));
	if ((code = pcre_compile(args[1].c_str(),
				 PCRE_CASELESS,
				 &errptr,
				 &eoff,
				 NULL)) == NULL) {
	    retval = "error: " + string(errptr);
	}
	else {
	    textview_curses *tc = lnav_data.ld_view_stack.top();
	    textview_curses::highlighter hl(code, false);

	    textview_curses::highlight_map_t &hm = tc->get_highlights();

	    hm[args[1]] = hl;

	    retval = "info: highlight pattern now active";
	}
    }

    return retval;
}

static string com_graph(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to graph";

    if (args.size() == 0) {
	args.push_back("graph");
    }
    else if (args.size() > 1) {
	const char *errptr;
	pcre       *code;
	int        eoff;

	args[1] = cmdline.substr(cmdline.find(args[1]));
	if ((code = pcre_compile(args[1].c_str(),
				 PCRE_CASELESS,
				 &errptr,
				 &eoff,
				 NULL)) == NULL) {
	    retval = "error: " + string(errptr);
	}
	else {
	    textview_curses &tc = lnav_data.ld_views[LNV_LOG];
	    textview_curses::highlighter hl(code, true);

	    textview_curses::highlight_map_t &hm = tc.get_highlights();

	    hm["(graph"] = hl;
	    lnav_data.ld_graph_source.set_highlighter(&hm["(graph"]);

	    auto_ptr<grep_proc> gp(new grep_proc(code,
						 tc,
						 lnav_data.ld_max_fd,
						 lnav_data.ld_read_fds));

	    gp->queue_request();
	    gp->start();
	    gp->set_sink(&lnav_data.ld_graph_source);

	    auto_ptr<grep_highlighter>
	    gh(new grep_highlighter(gp, "(graph", hm));
	    lnav_data.ld_grep_child[LG_GRAPH] = gh;

	    retval = "";
	}
    }

    return retval;
}

class pcre_filter
    : public logfile_filter {
public:
    pcre_filter(type_t type, string id, pcre *code)
	: logfile_filter(type, id),
	  pf_code(code) { };
    virtual ~pcre_filter() { };

    bool matches(string line)
    {
	static const int MATCH_COUNT = 20 * 3;
	int  matches[MATCH_COUNT], rc;
	bool retval;

	rc = pcre_exec(this->pf_code,
		       NULL,
		       line.c_str(),
		       line.size(),
		       0,
		       0,
		       matches,
		       MATCH_COUNT);
	retval = (rc >= 0);

#if 0
	fprintf(stderr, " out %d %s\n",
		retval,
		line.c_str());
#endif

	return retval;
    };

protected:
    auto_mem<pcre> pf_code;
};

static string com_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to filter out";

    if (args.size() == 0) {
	args.push_back("filter");
    }
    else if (args.size() > 1) {
	const char *errptr;
	pcre       *code;
	int        eoff;

	args[1] = cmdline.substr(cmdline.find(args[1]));
	if ((code = pcre_compile(args[1].c_str(),
				 0,
				 &errptr,
				 &eoff,
				 NULL)) == NULL) {
	    retval = "error: " + string(errptr);
	}
	else {
	    logfile_sub_source &lss = lnav_data.ld_log_source;
	    logfile_filter::type_t lt = (args[0] == "filter-out") ?
					logfile_filter::EXCLUDE : logfile_filter::INCLUDE;
	    auto_ptr<pcre_filter> pf(new pcre_filter(lt, args[1], code));

	    lss.get_filters().push_back(pf.release());
	    lnav_data.ld_rl_view->
	    add_possibility(LNM_COMMAND, "enabled-filter", args[1]);
	    rebuild_indexes(true);

	    retval = "info: filter now active";
	}
    }

    return retval;
}

static string com_enable_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting disabled filter to enable";

    if (args.size() == 0) {
	args.push_back("disabled-filter");
    }
    else if (args.size() > 1) {
	logfile_filter *lf;

	args[1] = cmdline.substr(cmdline.find(args[1]));
	lf      = lnav_data.ld_log_source.get_filter(args[1]);
	if (lf == NULL) {
	    retval = "error: no such filter -- " + args[1];
	}
	else if (lf->is_enabled()) {
	    retval = "info: filter already enabled";
	}
	else {
	    lf->enable();
	    lnav_data.ld_rl_view->
	    rem_possibility(LNM_COMMAND, "disabled-filter", args[1]);
	    lnav_data.ld_rl_view->
	    add_possibility(LNM_COMMAND, "enabled-filter", args[1]);
	    rebuild_indexes(true);
	    retval = "info: filter enabled";
	}
    }

    return retval;
}

static string com_disable_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting enabled filter to disable";

    if (args.size() == 0) {
	args.push_back("enabled-filter");
    }
    else if (args.size() > 1) {
	logfile_filter *lf;

	args[1] = cmdline.substr(cmdline.find(args[1]));
	lf      = lnav_data.ld_log_source.get_filter(args[1]);
	if (lf == NULL) {
	    retval = "error: no such filter -- " + args[1];
	}
	else if (!lf->is_enabled()) {
	    retval = "info: filter already disabled";
	}
	else {
	    lf->disable();
	    lnav_data.ld_rl_view->
	    rem_possibility(LNM_COMMAND, "disabled-filter", args[1]);
	    lnav_data.ld_rl_view->
	    add_possibility(LNM_COMMAND, "enabled-filter", args[1]);
	    rebuild_indexes(true);
	    retval = "info: filter disabled";
	}
    }

    return retval;
}

static string com_capture(string cmdline, vector<string> &args)
{
    string retval = "error: expecting table name";

    if (args.size() == 2) {
	lnav_data.ld_mode = LNM_CAPTURE;
	lnav_data.ld_rl_view->focus(LNM_CAPTURE, "index: ");

	retval = "";
    }

    return retval;
}

static readline_context::command_map_t lnav_commands;

static int sql_callback(void *arg,
			int ncols,
			char **values,
			char **colnames)
{
    db_label_source &dls = lnav_data.ld_db_rows;
    hist_source &hs = lnav_data.ld_db_source;
    int row_number;
    int lpc, retval = 0;

    row_number = dls.dls_rows.size();
    dls.dls_rows.resize(row_number + 1);
    if (dls.dls_headers.empty()) {
	for (lpc = 0; lpc < ncols; lpc++) {
	    dls.push_header(colnames[lpc]);
	    hs.set_role_for_type(bucket_type_t(lpc),
				 view_colors::singleton().
				 next_highlight());
	}
    }
    for (lpc = 0; lpc < ncols; lpc++) {
	double num_value = 0.0;
	
	dls.push_column(values[lpc]);
	if (strcmp(colnames[lpc], "line_number") != 0)
	    sscanf(values[lpc], "%lf", &num_value);
	hs.add_value(row_number, bucket_type_t(lpc), num_value);
    }
    
    return retval;
}

static void rl_search(void *dummy, readline_curses *rc)
{
    static string last_search[LNV__MAX];

    string name;

    switch (lnav_data.ld_mode) {
    case LNM_SEARCH:
	name = "(search";
	break;
    case LNM_CAPTURE:
	assert(0);
	name = "(capture";
	break;
	
    case LNM_COMMAND:
	return;
	
    case LNM_SQL:
	if (!sqlite3_complete(rc->get_value().c_str())) {
	    lnav_data.ld_bottom_source.
		grep_error("sql error: incomplete statement");
	}
	else {
	    sqlite3_stmt *stmt;
	    const char *tail;
	    int retcode;
	    
	    retcode = sqlite3_prepare_v2(lnav_data.ld_db,
					 rc->get_value().c_str(),
					 -1,
					 &stmt,
					 &tail);
	    if (retcode != SQLITE_OK) {
		const char *errmsg = sqlite3_errmsg(lnav_data.ld_db);
		
		lnav_data.ld_bottom_source.
		    grep_error(string("sql error: ") + string(errmsg));
	    }
	    else {
		lnav_data.ld_bottom_source.
		    grep_error("");
	    }
	}
	return;
    default:
	assert(0);
	break;
    }

    textview_curses *tc = lnav_data.ld_view_stack.top();
    int index = (tc - lnav_data.ld_views);
    auto_ptr<grep_highlighter> &gc = lnav_data.ld_search_child[index];
    
    if ((gc.get() == NULL) || (rc->get_value() != last_search[index])) {
	const char      *errptr;
	pcre            *code;
	int             eoff;
	
	if (rc->get_value().empty() && gc.get() != NULL) {
	    tc->grep_begin(*(gc->get_grep_proc()));
	    tc->grep_end(*(gc->get_grep_proc()));
	}
	gc.reset();
	
	fprintf(stderr, "start search for: %s\n", rc->get_value().c_str());

	tc->set_top(lnav_data.ld_search_start_line);
	tc->match_reset();
	
	if (rc->get_value().empty()) {
	    lnav_data.ld_bottom_source.grep_error("");
	}
	else if ((code = pcre_compile(rc->get_value().c_str(),
				      PCRE_CASELESS,
				      &errptr,
				      &eoff,
				      NULL)) == NULL) {
	    lnav_data.ld_bottom_source.
		grep_error("regexp error: " + string(errptr));
	}
	else {
	    textview_curses::highlighter
		hl(code, false, view_colors::VCR_SEARCH);
	    
	    textview_curses::highlight_map_t &hm = tc->get_highlights();
	    hm[name] = hl;
	    
	    auto_ptr<grep_proc> gp(new grep_proc(code,
						 *tc,
						 lnav_data.ld_max_fd,
						 lnav_data.ld_read_fds));
	    
	    gp->queue_request(grep_line_t(tc->get_top()));
	    if (tc->get_top() > 0) {
		gp->queue_request(grep_line_t(0),
				  grep_line_t(tc->get_top()));
	    }
	    gp->start();
	    gp->set_sink(lnav_data.ld_view_stack.top());
	    
	    tc->set_follow_search(true);
	    
	    auto_ptr<grep_highlighter> gh(new grep_highlighter(gp, name, hm));
	    gc = gh;
	    
	    last_search[index] = rc->get_value();
	}
    }
}

static void rl_callback(void *dummy, readline_curses *rc)
{
    switch (lnav_data.ld_mode) {
    case LNM_PAGING:
	assert(0);
	break;
	
    case LNM_COMMAND:
	{
	    stringstream ss(rc->get_value());

	    vector<string> args;
	    string buf, msg;

	    lnav_data.ld_mode = LNM_PAGING;
	    while (ss >> buf) {
		args.push_back(buf);
	    }

	    if (args.size() > 0) {
		readline_context::command_map_t::iterator iter;

		if ((iter = lnav_commands.find(args[0])) ==
		    lnav_commands.end()) {
		    msg = "error: unknown command - " + args[0];
		}
		else {
		    msg = iter->second(rc->get_value(), args);
		}
	    }
	    rc->set_value(msg);
	}
	break;

    case LNM_SEARCH:
    case LNM_CAPTURE:
	rl_search(dummy, rc);
	if (rc->get_value().size() > 0) {
	    lnav_data.ld_view_stack.top()->set_follow_search(false);
	    lnav_data.ld_rl_view->
	    add_possibility(LNM_COMMAND, "filter", rc->get_value());
	    rc->set_value("search: " + rc->get_value());
	}
	lnav_data.ld_mode = LNM_PAGING;
	break;

    case LNM_SQL:
	{
	    db_label_source &dls = lnav_data.ld_db_rows;
	    hist_source &hs = lnav_data.ld_db_source;
	    auto_mem<char, sqlite3_free> errmsg;
	    
	    lnav_data.ld_bottom_source.grep_error("");
	    hs.clear();
	    dls.dls_headers.clear();
	    dls.dls_rows.clear();
	    if (sqlite3_exec(lnav_data.ld_db,
			     rc->get_value().c_str(),
			     sql_callback,
			     NULL,
			     errmsg.out()) != SQLITE_OK) {
		rc->set_value(errmsg.in());
	    }
	    else {
		rc->set_value("");
		
		hs.analyze();
		lnav_data.ld_views[LNV_DB].reload_data();
		
		if (dls.dls_rows.size() > 0) {
		    ensure_view(&lnav_data.ld_views[LNV_DB]);
		}
	    }
	}

	lnav_data.ld_mode = LNM_PAGING;
	break;
    }
}

static void usage(void)
{
    const char *usage_msg =
	"usage: %s [-habfso] [logfile1 logfile2 ...]\n"
	"\n"
	"A curses-based log file viewer that indexes log messages by type\n"
	"and time to make it easier to navigate through files quickly.\n"
	"\n"
	"Key bindings:\n"
	"  ?     View/leave the online help text.\n"
	"  q     Quit the program.\n"
	"\n"
	"Options:\n"
	"  -h         Print this message, then exit.\n"
	"  -V         Print version information.\n"
	"  -s         Load the most recent syslog messages file.\n"
	"  -a         Load all of the most recent log file types.\n"
	"  -r         Load older rotated log files as well.\n"
	"\n"
	"Optional arguments:\n"
	"  logfile1          The log files to view.\n"
	"\n"
	"Examples:\n"
	"  To load and follow the syslog file -\n"
	"    $ lnav -s\n"
	"\n"
	"Version: " PACKAGE_STRING "\n";

    fprintf(stderr, usage_msg, lnav_data.ld_program_name);
}

static pcre *xpcre_compile(const char *pattern, int options = 0)
{
    const char *errptr;
    pcre       *retval;
    int        eoff;

    if ((retval = pcre_compile(pattern,
			       options,
			       &errptr,
			       &eoff,
			       NULL)) == NULL) {
	fprintf(stderr, "internal error: failed to compile -- %s\n", pattern);
	fprintf(stderr, "internal error: %s\n", errptr);

	exit(1);
    }

    return retval;
}

static void update_times(void *, listview_curses *lv)
{
    if (lv == &lnav_data.ld_views[LNV_LOG] && lv->get_inner_height() > 0) {
	logfile_sub_source &lss = lnav_data.ld_log_source;

	lnav_data.ld_top_time =
	    lss.find_line(lss.at(lv->get_top()))->get_time();
	lnav_data.ld_bottom_time =
	    lss.find_line(lss.at(lv->get_bottom()))->get_time();
    }
    if (lv == &lnav_data.ld_views[LNV_HISTOGRAM] &&
	lv->get_inner_height() > 0) {
	hist_source &hs = lnav_data.ld_hist_source;

	lnav_data.ld_top_time    = hs.value_for_row(lv->get_top());
	lnav_data.ld_bottom_time = hs.value_for_row(lv->get_bottom());
    }
}

static void looper(void)
{
    int fd;

    fd = open(lnav_data.ld_debug_log_name, O_WRONLY | O_CREAT | O_APPEND, 0666);
    dup2(fd, STDERR_FILENO);
    close(fd);
    fprintf(stderr, "startup\n");

    try {
	readline_context command_context("cmd", &lnav_commands);

	readline_context search_context("search");
	readline_context index_context("capture");
	readline_context sql_context("sql");
	textview_curses  *tc;
	readline_curses  rlc;
	int lpc;

	listview_curses::action::broadcaster &sb =
	    lnav_data.ld_scroll_broadcaster;

	rlc.add_context(LNM_COMMAND, command_context);
	rlc.add_context(LNM_SEARCH, search_context);
	rlc.add_context(LNM_CAPTURE, index_context);
	rlc.add_context(LNM_SQL, sql_context);
	rlc.start();

	lnav_data.ld_rl_view = &rlc;

	lnav_data.ld_rl_view->
	add_possibility(LNM_COMMAND, "graph", "\\d+(?:\\.\\d+)?");
	lnav_data.ld_rl_view->
	add_possibility(LNM_COMMAND, "graph", "([:= \\t]\\d+(?:\\.\\d+)?)");

	{
	    const char *sql_commands[] = {
		"add",
		"all",
		"alter",
		"analyze",
		"asc",
		"attach",
		"begin",
		"collate",
		"column",
		"commit",
		"conflict",
		"create",
		"cross",
		"database",
		"delete",
		"desc",
		"detach",
		"distinct",
		"drop",
		"end",
		"except",
		"explain",
		"from",
		"group",
		"having",
		"idle_msecs",
		"index",
		"indexed",
		"inner",
		"insert",
		"intersect",
		"join",
		"left",
		"limit",
		"natural",
		"offset",
		"order",
		"outer",
		"pragma",
		"reindex",
		"rename",
		"replace",
		"rollback",
		"select",
		"table",
		"transaction",
		"trigger",
		"union",
		"unique",
		"update",
		"using",
		"vacuum",
		"view",
		"where",
		"when",

		// XXX do the following dynamically by reading sqlite_master

		"access_log",
		"syslog_log",
		"generic_log",
		"strace_log",

		"line_number",
		"path",
		"log_time",
		"level",
		"raw_line",
		
		"c_ip",
		"cs_username",
		"cs_method",
		"cs_uri_stem",
		"cs_uri_query",
		"cs_version",
		"sc_status",
		"sc_bytes",
		"cs_referer",
		"cs_user_agent",
		
		"funcname",
		"result",
		"duration",
		"arg0",
		"arg1",
		"arg2",
		"arg3",
		"arg4",
		"arg5",
		"arg6",
		"arg7",
		"arg8",
		"arg9",
		
		NULL
	    };

	    for (int lpc = 0; sql_commands[lpc]; lpc++) {
		lnav_data.ld_rl_view->
		    add_possibility(LNM_SQL, "*", sql_commands[lpc]);
	    }
	}

	(void)signal(SIGINT, sigint);
	(void)signal(SIGTERM, sigint);
	(void)signal(SIGWINCH, sigwinch);

	screen_curses sc;

	lnav_data.ld_window = sc.get_window();
	keypad(stdscr, TRUE);
	(void)nonl();
	(void)cbreak();
	(void)noecho();
	(void)nodelay(lnav_data.ld_window, 1);

	define_key("\033Od", KEY_BEG);
	define_key("\033Oc", KEY_END);

	view_colors::singleton().init();

	rlc.set_window(lnav_data.ld_window);
	rlc.set_y(-1);
	rlc.set_perform_action(readline_curses::action(rl_callback));
	rlc.set_timeout_action(readline_curses::action(rl_search));

	lnav_data.ld_view_stack.push(&lnav_data.ld_views[LNV_LOG]);

	tc = lnav_data.ld_view_stack.top();

	for (lpc = 0; lpc < LNV__MAX; lpc++) {
	    lnav_data.ld_views[lpc].set_window(lnav_data.ld_window);
	    lnav_data.ld_views[lpc].set_y(1);
	    lnav_data.ld_views[lpc].
	    set_height(vis_line_t(-(rlc.get_height() + 1 + 1)));
	    lnav_data.ld_views[lpc].
	    set_scroll_action(sb.get_functor());
	    lnav_data.ld_views[lpc].
	    set_search_action(&lnav_data.ld_bottom_source.hits_wire);
	}

	lnav_data.ld_status[LNS_TOP].set_top(0);
	for (lpc = 0; lpc < LNS__MAX; lpc++) {
	    lnav_data.ld_status[lpc].set_window(lnav_data.ld_window);
	}
	lnav_data.ld_status[LNS_TOP].
	set_data_source(&lnav_data.ld_top_source);
	lnav_data.ld_status[LNS_BOTTOM].
	set_data_source(&lnav_data.ld_bottom_source);
	sb.push_back(view_action<listview_curses>(update_times));
	sb.push_back(&lnav_data.ld_top_source.marks_wire);
	sb.push_back(&lnav_data.ld_top_source.filename_wire);
	sb.push_back(&lnav_data.ld_bottom_source.line_number_wire);
	sb.push_back(&lnav_data.ld_bottom_source.percent_wire);
	sb.push_back(&lnav_data.ld_bottom_source.marks_wire);

	{
	    vis_line_t top(0), height(0);

	    unsigned long width;

	    tc->get_dimensions(height, width);
	    top = vis_line_t(tc->get_inner_height()) - height + vis_line_t(1);
	    if (top > 0) {
		tc->set_top(top);
	    }
	}

	{
	    hist_source &hs = lnav_data.ld_hist_source;

	    lnav_data.ld_hist_zoom = 2;
	    hs.set_role_for_type(bucket_type_t(logline::LEVEL_CRITICAL),
				 view_colors::VCR_ERROR);
	    hs.set_role_for_type(bucket_type_t(logline::LEVEL_ERROR),
				 view_colors::VCR_ERROR);
	    hs.set_role_for_type(bucket_type_t(logline::LEVEL_WARNING),
				 view_colors::VCR_WARNING);
	    hs.set_label_source(new time_label_source());
	}

	{
	    hist_source &hs = lnav_data.ld_graph_source;

	    hs.set_bucket_size(1);
	    hs.set_group_size(100);
	}

	{
	    hist_source &hs = lnav_data.ld_db_source;

	    hs.set_bucket_size(1);
	    hs.set_group_size(100);
	    hs.set_label_source(&lnav_data.ld_db_rows);
	}

	FD_ZERO(&lnav_data.ld_read_fds);
	FD_SET(STDIN_FILENO, &lnav_data.ld_read_fds);
	lnav_data.ld_max_fd =
	    max(STDIN_FILENO, rlc.update_fd_set(lnav_data.ld_read_fds));

	while (lnav_data.ld_looping) {
	    fd_set         ready_rfds = lnav_data.ld_read_fds;
	    struct timeval to         = { 0, 330000 };
	    int            rc;

	    lnav_data.ld_top_source.update_time();

	    for (lpc = 0; lpc < LNV__MAX; lpc++) {
		lnav_data.ld_views[lpc]
		.set_height(vis_line_t(-(rlc.get_height() + 1)));
	    }
	    lnav_data.ld_status[LNS_BOTTOM].set_top(-(rlc.get_height() + 1));

	    lnav_data.ld_view_stack.top()->do_update();
	    lnav_data.ld_status[LNS_TOP].do_update();
	    lnav_data.ld_status[LNS_BOTTOM].do_update();
	    rlc.do_update();
	    refresh();

	    rc = select(lnav_data.ld_max_fd + 1,
			&ready_rfds, NULL, NULL,
			&to);
	    
	    lnav_data.ld_bottom_source.
		update_hits(lnav_data.ld_view_stack.top());

	    if (rc < 0) {
		switch (errno) {
		case EINTR:
		    break;

		default:
		    fprintf(stderr, "select %s\n", strerror(errno));
		    lnav_data.ld_looping = false;
		    break;
		}
	    }
	    else if (rc == 0) {
		static bool initial_build = false;
		
		rebuild_indexes(false);
		if (!initial_build &&
		    lnav_data.ld_log_source.text_line_count() == 0 &&
		    lnav_data.ld_text_source.text_line_count() > 0) {
		    toggle_view(&lnav_data.ld_views[LNV_TEXT]);
		    lnav_data.ld_views[LNV_TEXT].set_top(vis_line_t(0));
		}
		initial_build = true;
	    }
	    else {
		if (FD_ISSET(STDIN_FILENO, &ready_rfds)) {
		    int ch;

		    while ((ch = getch()) != ERR) {
			switch (ch) {
			case CEOF:
			case KEY_RESIZE:
			    break;

			default:
			    switch (lnav_data.ld_mode) {
			    case LNM_PAGING:
				handle_paging_key(ch);
				break;

			    case LNM_COMMAND:
			    case LNM_SEARCH:
			    case LNM_CAPTURE:
			    case LNM_SQL:
				handle_rl_key(ch);
				break;

			    default:
				assert(0);
				break;
			    }
			    break;
			}
		    }
		}
		for (lpc = 0; lpc < LG__MAX; lpc++) {
		    auto_ptr<grep_highlighter> &gc =
			lnav_data.ld_grep_child[lpc];
		    
		    if (gc.get() != NULL) {
			gc->get_grep_proc()->check_fd_set(ready_rfds);
			if (lpc == LG_GRAPH) {
			    lnav_data.ld_views[LNV_GRAPH].reload_data();
			    /* XXX */
			}
		    }
		}
		for (lpc = 0; lpc < LNV__MAX; lpc++) {
		    auto_ptr<grep_highlighter> &gc =
			lnav_data.ld_search_child[lpc];
		    
		    if (gc.get() != NULL) {
			gc->get_grep_proc()->check_fd_set(ready_rfds);
		    }
		}
		rlc.check_fd_set(ready_rfds);
	    }

	    if (lnav_data.ld_winched) {
		struct winsize size;

		fprintf(stderr, "WINCHED\n");

		if (ioctl(fileno(stdout), TIOCGWINSZ, &size) == 0) {
		    resizeterm(size.ws_row, size.ws_col);
		}
		rlc.window_change();
		lnav_data.ld_view_stack.top()->set_needs_update();
		lnav_data.ld_winched = false;
	    }
	}
    }
    catch (readline_curses::error & e) {
	fprintf(stderr, "error: %s\n", strerror(e.e_err));
    }
}

class access_log_table : public log_vtab_impl {
public:
    
    access_log_table()
	: log_vtab_impl("access_log"),
	  alt_regex("([\\w\\.-]+) [\\w\\.-]+ ([\\w\\.-]+) "
		    "\\[[^\\]]+\\] \"(\\w+) ([^ \\?]+)(\\?[^ ]+)? "
		    "([\\w/\\.]+)\" (\\d+) "
		    "(\\d+|-)(?: \"([^\"]+)\" \"([^\"]+)\")?.*") {
    };
    
    void get_columns(vector<vtab_column> &cols) {
	cols.push_back(vtab_column("c_ip", "text"));
	cols.push_back(vtab_column("cs_username", "text"));
	cols.push_back(vtab_column("cs_method", "text"));
	cols.push_back(vtab_column("cs_uri_stem", "text"));
	cols.push_back(vtab_column("cs_uri_query", "text"));
	cols.push_back(vtab_column("cs_version", "text"));
	cols.push_back(vtab_column("sc_status", "text"));
	cols.push_back(vtab_column("sc_bytes", "int"));
	cols.push_back(vtab_column("cs_referer", "text"));
	cols.push_back(vtab_column("cs_user_agent", "text"));
    };

    void extract(const std::string &line,
		 int column,
		 sqlite3_context *ctx) {
	string c_ip, cs_username, cs_method, cs_uri_stem, cs_uri_query;
	string cs_version, sc_status, cs_referer, cs_user_agent;
	string sc_bytes;
	
	if (!this->alt_regex.FullMatch(line,
				       &c_ip,
				       &cs_username,
				       &cs_method,
				       &cs_uri_stem,
				       &cs_uri_query,
				       &cs_version,
				       &sc_status,
				       &sc_bytes,
				       &cs_referer,
				       &cs_user_agent)) {
	    fprintf(stderr, "bad match! %d %s\n", column, line.c_str());
	}
	switch (column) {
	case 0:
	    sqlite3_result_text(ctx,
				c_ip.c_str(),
				c_ip.length(),
				SQLITE_TRANSIENT);
	    break;
	case 1:
	    sqlite3_result_text(ctx,
				cs_username.c_str(),
				cs_username.length(),
				SQLITE_TRANSIENT);
	    break;
	case 2:
	    sqlite3_result_text(ctx,
				cs_method.c_str(),
				cs_method.length(),
				SQLITE_TRANSIENT);
	    break;
	case 3:
	    sqlite3_result_text(ctx,
				cs_uri_stem.c_str(),
				cs_uri_stem.length(),
				SQLITE_TRANSIENT);
	    break;
	case 4:
	    sqlite3_result_text(ctx,
				cs_uri_query.c_str(),
				cs_uri_query.length(),
				SQLITE_TRANSIENT);
	    break;
	case 5:
	    sqlite3_result_text(ctx,
				cs_version.c_str(),
				cs_version.length(),
				SQLITE_TRANSIENT);
	    break;
	case 6:
	    sqlite3_result_text(ctx,
				sc_status.c_str(),
				sc_status.length(),
				SQLITE_TRANSIENT);
	    break;
	case 7:
	    {
		int sc_bytes_int = 0;

		sscanf(sc_bytes.c_str(), "%d", &sc_bytes_int);
		sqlite3_result_int64(ctx, sc_bytes_int);
	    }
	    break;
	case 8:
	    sqlite3_result_text(ctx,
				cs_referer.c_str(),
				cs_referer.length(),
				SQLITE_TRANSIENT);
	    break;
	case 9:
	    sqlite3_result_text(ctx,
				cs_user_agent.c_str(),
				cs_user_agent.length(),
				SQLITE_TRANSIENT);
	    break;
	}
    };

private:
    pcrecpp::RE alt_regex;
};

class strace_log_table : public log_vtab_impl {
public:
    
    strace_log_table()
	: log_vtab_impl("strace_log"),
	  slt_regex("[0-9:.]* ([a-zA-Z_][a-zA-Z_0-9]*)\\("
		    "(.*)\\)"
		    "\\s+= ([-xa-fA-F\\d\\?]+).*(?:<(\\d+\\.\\d+)>)?") {
    };
    
    void get_columns(vector<vtab_column> &cols) {
	cols.push_back(vtab_column("funcname", "text"));
	cols.push_back(vtab_column("result", "text"));
	cols.push_back(vtab_column("duration", "text"));
	cols.push_back(vtab_column("arg0", "text"));
	cols.push_back(vtab_column("arg1", "text"));
	cols.push_back(vtab_column("arg2", "text"));
	cols.push_back(vtab_column("arg3", "text"));
	cols.push_back(vtab_column("arg4", "text"));
	cols.push_back(vtab_column("arg5", "text"));
	cols.push_back(vtab_column("arg6", "text"));
	cols.push_back(vtab_column("arg7", "text"));
	cols.push_back(vtab_column("arg8", "text"));
	cols.push_back(vtab_column("arg9", "text"));
    };

    void extract(const std::string &line,
		 int column,
		 sqlite3_context *ctx) {
	string function, args, result, duration = "0";
	
	if (!this->slt_regex.FullMatch(line,
				       &function,
				       &args,
				       &result,
				       &duration)) {
	    fprintf(stderr, "bad match! %s\n", line.c_str());
	}
	switch (column) {
	case 0:
	    sqlite3_result_text(ctx,
				function.c_str(),
				function.length(),
				SQLITE_TRANSIENT);
	    break;
	case 1:
	    sqlite3_result_text(ctx,
				result.c_str(),
				result.length(),
				SQLITE_TRANSIENT);
	    break;
	case 2:
	    sqlite3_result_text(ctx,
				duration.c_str(),
				duration.length(),
				SQLITE_TRANSIENT);
	    break;
	default:
	    {
		const char *arg_start = args.c_str();
		int in_struct = 0, in_list = 0;
		int lpc, argnum, curarg = 0;
		bool in_quote = false;

		argnum = column - 3;
		for (lpc = 0; lpc < (int)args.length(); lpc++) {
		    switch (args[lpc]) {
		    case '{':
			if (!in_quote)
			    in_struct += 1;
			break;
		    case '}':
			if (!in_quote)
			    in_struct -= 1;
			break;
		    case '[':
			if (!in_quote)
			    in_list += 1;
			break;
		    case ']':
			if (!in_quote)
			    in_list -= 1;
			break;
		    case '"':
			if (!in_quote)
			    in_quote = true;
			else if (lpc > 0 && args[lpc - 1] != '\\')
			    in_quote = false;
			break;
		    case ',':
			if (!in_quote && !in_struct && !in_list) {
			    if (curarg == argnum) {
				sqlite3_result_text(ctx,
						    arg_start,
						    &(args.c_str()[lpc]) -
						    arg_start,
						    SQLITE_TRANSIENT);
				return;
			    }
			    
			    curarg += 1;
			    arg_start = &(args.c_str()[lpc + 1]);
			}
			break;
		    }
		}
		if (curarg == argnum) {
		    sqlite3_result_text(ctx,
					arg_start,
					&(args.c_str()[lpc]) -
					arg_start,
					SQLITE_TRANSIENT);
		}
		else {
		    sqlite3_result_text(ctx,
					"",
					0,
					SQLITE_TRANSIENT);
		}
	    }
	    break;
	}
    };

private:
    pcrecpp::RE slt_regex;
};

void ensure_dotlnav(void)
{
    char *home;
    
    home = getenv("HOME");
    if (home) {
	char hpath[2048];

	snprintf(hpath, sizeof(hpath), "%s/.lnav", home);
	mkdir(hpath, 0755);
    }
}

int main(int argc, char *argv[])
{
    int lpc, c, retval = EXIT_SUCCESS;
    auto_ptr<piper_proc> stdin_reader;

    ensure_dotlnav();
    
    if (sqlite3_open(":memory:", lnav_data.ld_db.out()) != SQLITE_OK) {
	fprintf(stderr, "unable to create sqlite memory database\n");
	exit(EXIT_FAILURE);
    }
 
    lnav_data.ld_program_name = argv[0];

    lnav_data.ld_vtab_manager =
	new log_vtab_manager(lnav_data.ld_db, lnav_data.ld_log_source);

    lnav_data.ld_vtab_manager->register_vtab(new log_vtab_impl("syslog_log"));
    lnav_data.ld_vtab_manager->register_vtab(new log_vtab_impl("generic_log"));
    lnav_data.ld_vtab_manager->register_vtab(new access_log_table());
    lnav_data.ld_vtab_manager->register_vtab(new strace_log_table());
    
    DEFAULT_FILES.insert(make_pair(LNF_SYSLOG, string("var/log/messages")));

    lnav_commands["unix-time"]      = com_unix_time;
    lnav_commands["current-time"]   = com_current_time;
    lnav_commands["goto"]           = com_goto;
    lnav_commands["graph"]          = com_graph;
    lnav_commands["highlight"]      = com_highlight;
    lnav_commands["filter-in"]      = com_filter;
    lnav_commands["filter-out"]     = com_filter;
    lnav_commands["append-to"] = com_save_to;
    lnav_commands["write-to"] = com_save_to;
    lnav_commands["enable-filter"]  = com_enable_filter;
    lnav_commands["disable-filter"] = com_disable_filter;
    lnav_commands["capture-into"] = com_capture;

    lnav_data.ld_views[LNV_HELP].
    set_sub_source(new plain_text_source(help_text_start));
    lnav_data.ld_views[LNV_LOG].
    set_sub_source(&lnav_data.ld_log_source);
    lnav_data.ld_views[LNV_TEXT].
    set_sub_source(&lnav_data.ld_text_source);
    lnav_data.ld_views[LNV_HISTOGRAM].
    set_sub_source(&lnav_data.ld_hist_source);
    lnav_data.ld_views[LNV_GRAPH].
    set_sub_source(&lnav_data.ld_graph_source);
    lnav_data.ld_views[LNV_DB].
    set_sub_source(&lnav_data.ld_db_source);

    {
	textview_curses::highlight_map_t &hm =
	    lnav_data.ld_views[LNV_LOG].get_highlights();

	hm["(sql"] = textview_curses::
		     highlighter(xpcre_compile("(?: alter | select | insert | update "
					       "| create "
					       "| from | where | order by "
					       "| group by )", PCRE_CASELESS));
	hm["(java"] = textview_curses::
		      highlighter(xpcre_compile("(?:\\w+\\.java:\\d+)"));
	hm["(xml"] = textview_curses::
		     highlighter(xpcre_compile("<(/?[^ >]+)[^>]*>"));
	hm["(stringd"] = textview_curses::
			 highlighter(xpcre_compile("\".*(?<!\\\\)\""));
	hm["(strings"] = textview_curses::
			 highlighter(xpcre_compile("\'.*(?<!\\\\)\'"));
	hm["(ip"] = textview_curses::
		    highlighter(xpcre_compile("\\d+\\.\\d+\\.\\d+\\.\\d+"));
    }

    lnav_data.ld_looping = true;
    lnav_data.ld_mode    = LNM_PAGING;
    lnav_data.ld_debug_log_name = "/dev/null"; // XXX change to /dev/null
    while ((c = getopt(argc, argv, "harsd:V")) != -1) {
	switch (c) {
	case 'h':
	    usage();
	    exit(retval);
	    break;

	case 'd':
	    lnav_data.ld_debug_log_name = optarg;
	    break;

	case 'a':
	    lnav_data.ld_flags |= LNF__ALL;
	    break;

	case 'r':
	    lnav_data.ld_flags |= LNF_ROTATED;
	    break;

	case 's':
	    lnav_data.ld_flags |= LNF_SYSLOG;
	    break;

	case 'V':
	    printf("%s\n", PACKAGE_STRING);
	    exit(0);
	    break;

	default:    
	    retval = EXIT_FAILURE;
	    break;
	}
    }

    argc -= optind;
    argv += optind;

    if (isatty(STDIN_FILENO) && argc == 0 && !(lnav_data.ld_flags & LNF__ALL)) {
	lnav_data.ld_flags |= LNF_SYSLOG;
    }

    if (lnav_data.ld_flags != 0) {
	char start_dir[FILENAME_MAX];

	if (getcwd(start_dir, sizeof(start_dir)) == NULL) {
	    perror("getcwd");
	}
	else {
	    do {
		for (lpc = 0; lpc < LNB__MAX; lpc++) {
		    if (!append_default_files((lnav_flags_t)(1L << lpc))) {
			retval = EXIT_FAILURE;
		    }
		}
	    } while (lnav_data.ld_file_names.empty() && change_to_parent_dir());

	    if (chdir(start_dir) == -1) {
		perror("chdir(start_dir)");
	    }
	}
    }

    for (lpc = 0; lpc < argc; lpc++) {
	lnav_data.ld_file_names.insert(make_pair(argv[lpc], -1));
    }

    if (!isatty(STDOUT_FILENO)) {
	fprintf(stderr, "error: stdout is not a tty.\n");
	retval = EXIT_FAILURE;
    }

    if (!isatty(STDIN_FILENO)) {
	stdin_reader = auto_ptr<piper_proc>(new piper_proc(STDIN_FILENO));
	lnav_data.ld_file_names.insert(make_pair("stdin",
						 stdin_reader->get_fd()));
	if (dup2(STDOUT_FILENO, STDIN_FILENO) == -1) {
	    perror("cannot dup stdout to stdin");
	}
    }
    
    if (lnav_data.ld_file_names.empty()) {
	fprintf(stderr, "error: no log files given/found.\n");
	retval = EXIT_FAILURE;
    }

    if (retval != EXIT_SUCCESS) {
	usage();
    }
    else {
	try {
	    set< pair<string, int> >::iterator iter;

	    for (iter = lnav_data.ld_file_names.begin();
		 iter != lnav_data.ld_file_names.end();
		 iter++) {
		logfile *lf = new logfile(iter->first, iter->second);

		lnav_data.ld_text_source.tss_files.push_back(lf);
	    }

	    guard_termios gt(STDIN_FILENO);
	    looper();
	}
	catch (line_buffer::error & e) {
	    fprintf(stderr, "error: %s\n", strerror(e.e_err));
	}
	catch (logfile::error & e) {
	    if (e.e_err != EINTR) {
		fprintf(stderr,
			"error: %s -- '%s'\n",
			strerror(e.e_err),
			e.e_filename.c_str());
	    }
	}
    }

    return retval;
}
