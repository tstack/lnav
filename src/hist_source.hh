/**
 * @file hist_source.hh
 */

#ifndef __hist_source_hh
#define __hist_source_hh

#include <map>
#include <string>
#include <vector>

#include "strong_int.hh"
#include "textview_curses.hh"

typedef float bucket_count_t;

/** Type for indexes into a group of buckets. */
STRONG_INT_TYPE(int, bucket_group);

/** Type used to differentiate values added to the same row in the histogram */
STRONG_INT_TYPE(int, bucket_type);

/**
 * A text source that displays data as a histogram using horizontal bars.  Data
 * is added to the histogram using the add_value() method.  Once all of the
 * values have been added, the analyze() method needs to be called to analyze
 * the data so that it can be displayed.
 *
 * For example, if the values (7, 3, 4, 2) were added, they would be displayed
 * like so:
 *
 *   ******
 *   ***
 *   ****
 *   **
 */
class hist_source
    : public text_sub_source {
public:
    typedef std::map<bucket_type_t, bucket_count_t> bucket_t;

    /**
     * Source for labels on each bucket and group.
     */
    class label_source {
public:
	virtual ~label_source() { };

	virtual void hist_label_for_group(int group,
					  std::string &label_out) { };

	virtual void hist_label_for_bucket(int bucket_start_value,
					   const bucket_t &bucket,
					   std::string &label_out) { };
    };

    hist_source();
    virtual ~hist_source() { };

    void set_bucket_size(unsigned int bs) {
    	assert(bs > 0);

    	this->hs_bucket_size = bs;
    };
    unsigned int get_bucket_size(void) const { return this->hs_bucket_size; };

    void set_group_size(unsigned int gs) {
    	assert(gs > 0);
    	this->hs_group_size = gs;
    };
    unsigned int get_group_size(void) const { return this->hs_group_size; };

    void set_label_source(label_source *hls)
    {
	this->hs_label_source = hls;
    }

    label_source *get_label_source(void)
    {
	return this->hs_label_source;
    };

    int buckets_per_group(void) const
    {
	return this->hs_group_size / this->hs_bucket_size;
    };

    void clear(void) { this->hs_groups.clear(); };

    size_t text_line_count()
    {
	return (this->buckets_per_group() + 1) * this->hs_groups.size();
    };

    void set_role_for_type(bucket_type_t bt, view_colors::role_t role)
    {
	this->hs_type2role[bt] = role;
    };
    const view_colors::role_t &get_role_for_type(bucket_type_t bt)
    {
	return this->hs_type2role[bt];
    };

    void text_value_for_line(textview_curses &tc,
			     int row,
			     std::string &value_out,
			     bool no_scrub);
    void text_attrs_for_line(textview_curses &tc,
			     int row,
			     string_attrs_t &value_out);

    int value_for_row(vis_line_t row)
    {
	int grow   = row / (this->buckets_per_group() + 1);
	int brow   = row % (this->buckets_per_group() + 1);
	int retval = 0;

	if (!this->hs_group_keys.empty()) {
	    bucket_group_t bg = this->hs_group_keys[grow];

	    if (brow > 0) {
		brow -= 1;
	    }
	    retval = (bg * this->hs_group_size) + (brow * this->hs_bucket_size);
	}

	return retval;
    };

    vis_line_t row_for_value(int value)
    {
	vis_line_t retval;

	if (!this->hs_group_keys.empty()) {
	    bucket_group_t bg(value / this->hs_group_size);

	    std::vector<bucket_group_t>::iterator lb;

	    lb = lower_bound(this->hs_group_keys.begin(),
			     this->hs_group_keys.end(),
			     bg);
	    retval = vis_line_t(distance(this->hs_group_keys.begin(), lb) *
				(this->buckets_per_group() + 1));
	    retval += vis_line_t(1 +
				 (value % this->hs_group_size) /
				 this->hs_bucket_size);
	}

	return retval;
    };

    /**
     * Add a value to the histogram.
     *
     * @param value The row in the histogram.
     * @param bt The type of data.
     * @param amount The amount to add to this row in the histogram.
     */
    void add_value(unsigned int value,
                   bucket_type_t bt,
                   bucket_count_t amount = 1.0);
    void analyze(void);

protected:
    typedef std::vector<bucket_t> bucket_array_t;

    std::map<bucket_type_t, view_colors::role_t> hs_type2role;

    std::map<bucket_group_t, bucket_array_t> hs_groups;
    std::vector<bucket_group_t> hs_group_keys;

    unsigned int   hs_bucket_size; /* hours */
    unsigned int   hs_group_size;  /* days */
    bucket_count_t hs_min_count;
    bucket_count_t hs_max_count;
    /** Indicates that all of the data has been analyze()'d. */
    bool hs_analyzed;
    label_source   *hs_label_source;

    bucket_t *hs_token_bucket;
};

#endif
