
#ifndef __xterm_mouse_hh
#define __xterm_mouse_hh

#include <stdlib.h>
#include <curses.h>
#include <string.h>

#include <string>

class mouse_behavior {

public:
	virtual ~mouse_behavior() { };

	virtual void mouse_event(int button, int x, int y) = 0;
};

class xterm_mouse {

public:
	static const int XT_MAGIC = 32;

	static const int XT_BUTTON1 = 0;
	static const int XT_BUTTON2 = 1;
	static const int XT_BUTTON3 = 2;
	static const int XT_BUTTON_RELEASE = 3;

	static const int XT_SCROLL_WHEEL_FLAG = 64;
	static const int XT_SCROLL_UP =
		XT_SCROLL_WHEEL_FLAG | XT_BUTTON1;
	static const int XT_SCROLL_DOWN =
		XT_SCROLL_WHEEL_FLAG | XT_BUTTON2;

    	static const int XT_BUTTON__MASK =
    		XT_SCROLL_WHEEL_FLAG |
		XT_BUTTON1 |
		XT_BUTTON2 |
		XT_BUTTON3;

	static const char *XT_TERMCAP;
	static const char *XT_TERMCAP_TRACKING;

	static bool is_available() {
		static const char *termname = getenv("TERM");
		bool retval = false;

		if (termname and strstr(termname, "xterm") != NULL) {
			retval = true;
		}
		return retval;
	};

	xterm_mouse() : xm_behavior(NULL) {
	};

	~xterm_mouse() {
		set_enabled(false);
	};

	void set_enabled(bool enabled) {
		if (is_available()) {
			putp(tparm((char *)XT_TERMCAP, enabled ? 1 : 0));
			putp(tparm((char *)XT_TERMCAP_TRACKING, enabled ? 1 : 0));
			this->xm_enabled = enabled;
		}
	};

	bool is_enabled() {
		return this->xm_enabled;
	};

	void set_behavior(mouse_behavior *mb) {
		this->xm_behavior = mb;
	};

	mouse_behavior *get_behavior() { return this->xm_behavior; };

	void handle_mouse(int ch) {
	    	int bstate = getch();
	    	int x = getch() - XT_MAGIC;
	    	int y = getch() - XT_MAGIC;

	    	bstate -= XT_MAGIC;

	    	if (this->xm_behavior) {
	    		this->xm_behavior->mouse_event(
	                        bstate & XT_BUTTON__MASK,
	                        x,
	                        y);
	    	}
	};

private:
	bool xm_enabled;
	mouse_behavior *xm_behavior;

};

#endif
