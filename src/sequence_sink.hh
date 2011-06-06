
#ifndef __sequence_sink_hh
#define __sequence_sink_hh

#include <map>

#include "bookmarks.hh"
#include "grep_proc.hh"
#include "sequence_matcher.hh"

class sequence_sink : public grep_proc_sink {
public:
    sequence_sink(sequence_matcher &sm, bookmark_vector &bv) :
	ss_matcher(sm),
	ss_bookmarks(bv) {
    };

    void grep_match(grep_proc &gp,
		    grep_line_t line,
		    int start,
		    int end) {
	this->ss_line_values.clear();
    };

    void grep_capture(grep_proc &gp,
		      grep_line_t line,
		      int start,
		      int end,
		      char *capture) {
	if (start == -1)
	    this->ss_line_values.push_back("");
	else
	    this->ss_line_values.push_back(std::string(capture));
    };

    void grep_match_end(grep_proc &gp, grep_line_t line) {
	sequence_matcher::id_t line_id;

	this->ss_matcher.identity(this->ss_line_values, line_id);

	std::vector<grep_line_t> &line_state = this->ss_state[line_id];
	if (this->ss_matcher.match(this->ss_line_values,
				   line_state,
				   line)) {
	    std::vector<grep_line_t>::iterator iter;
	    
	    for (iter = line_state.begin();
		 iter != line_state.end();
		 ++iter) {
		this->ss_bookmarks.insert_once(vis_line_t(*iter));
	    }
	    line_state.clear();
	}
    };

private:
    sequence_matcher &ss_matcher;
    bookmark_vector &ss_bookmarks;
    std::vector<std::string> ss_line_values;
    std::map< sequence_matcher::id_t, std::vector<grep_line_t> > ss_state;
};

#endif
