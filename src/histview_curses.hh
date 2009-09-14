
#ifndef __hist_controller_hh
#define __hist_controller_hh

#include <map>
#include <string>

#include "strong_int.hh"
#include "listview_curses.hh"

STRONG_INT_TYPE(int, bucket_group);
STRONG_INT_TYPE(int, bucket_count);

class hist_data_source {

public:
    virtual ~hist_data_source() { };

    virtual int hist_values(void) = 0;
    virtual void hist_value_for(int index, int &value_out) = 0;
    
};

class hist_label_source {

public:
    virtual ~hist_label_source() { };
    
    virtual void hist_label_for_group(int group, std::string &label_out) { };

};

class hist_controller : public list_data_source {

public:
    hist_controller();
    virtual ~hist_controller() { };

    void set_bucket_size(int bs) { this->hv_bucket_size = bs; };
    int get_bucket_size(void) const { return this->hv_bucket_size; };

    void set_group_size(int gs) { this->hv_group_size = gs; };
    int get_group_size(void) const { return this->hv_group_size; };

    void set_data_source(hist_data_source *hds) {
	this->hv_data_source = hds;
    };
    hist_data_source *get_data_source(void) { return this->hv_data_source; };

    void set_label_source(hist_label_source *hls) {
	this->hv_label_source = hls;
    }
    hist_label_source *get_label_source(void) {
	return this->hv_label_source;
    };

    size_t listview_rows(void) {
	return (this->hv_group_size / this->hv_bucket_size) *
	    this->hv_groups.size();
    };

    void listview_value_for_row(vis_line_t row, std::string &value_out);

    void reload_data(void);
    
private:
    typedef vector<bucket_count_t> buckets_t;
    
    map<bucket_group_t, buckets_t> hv_groups;
    int hv_bucket_size; // hours
    int hv_group_size; // days
    hist_data_source *hv_data_source;
    hist_label_source *hv_label_source;
    
};

#endif
