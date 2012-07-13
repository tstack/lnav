
#ifndef __grep_highlighter_hh
#define __grep_highlighter_hh

#include <string>
#include <memory>

#include "grep_proc.hh"
#include "textview_curses.hh"

class grep_highlighter {
public:
    grep_highlighter(std::auto_ptr < grep_proc > gp,
		     std::string hl_name,
		     textview_curses::highlight_map_t &hl_map)
	: gh_grep_proc(gp),
	  gh_hl_name(hl_name),
	  gh_hl_map(hl_map) { };

    ~grep_highlighter()
    {
	this->gh_hl_map.erase(this->gh_hl_map.find(this->gh_hl_name));
    };

    grep_proc *get_grep_proc() { return this->gh_grep_proc.get(); };

private:
    std::auto_ptr<grep_proc> gh_grep_proc;
    std::string gh_hl_name;
    textview_curses::highlight_map_t gh_hl_map;
};

#endif
