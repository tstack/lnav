
#ifndef _top_sys_status_source_hh
#define _top_sys_status_source_hh

#include <string>

#include "logfile_sub_source.hh"
#include "statusview_curses.hh"

class top_sys_status_source
  : public status_data_source
{
  
public:
  
    typedef enum {
	TSF_CPU,
	TSF_MEM,
	TSF_TRAF,

	TSF__MAX
    } field_t;

    top_sys_status_source() {
	static std::string names[TSF__MAX] = {
	    "#CPU",
	    "#Mem",
	    "#Traf",
	};
	
	int lpc;
	
	for (lpc = 0; lpc < TSF__MAX; lpc++) {
	    this->tss_fields[lpc].set_width(5);
	    this->tss_fields[lpc].set_value(names[lpc]);
	}
	this->tss_fields[TSF_CPU].set_role(view_colors::VCR_WARN_STATUS);
	this->tss_fields[TSF_MEM].set_role(view_colors::VCR_ALERT_STATUS);
	this->tss_fields[TSF_TRAF].set_role(view_colors::VCR_ACTIVE_STATUS);
    };
    
    size_t statusview_fields(void) { return TSF__MAX; };
    
    status_field &statusview_value_for_field(int field) {
	return this->tss_fields[field];
    };
    
private:
    telltale_field tss_fields[TSF__MAX];
  
};

#endif
