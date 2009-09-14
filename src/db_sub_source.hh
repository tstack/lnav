
#ifndef __db_sub_source_hh
#define __db_sub_source_hh

#include <string>
#include <vector>

#include "hist_source.hh"

class db_label_source : public hist_source::label_source {
public:
    db_label_source() { };

    ~db_label_source() { };

    void hist_label_for_bucket(int bucket_start_value,
			       const hist_source::bucket_t &bucket,
			       std::string &label_out) {
	/*
	 * start_value is the result rowid, each bucket type is a column value
	 * label_out should be the raw text output.
	 */

	label_out.clear();
	if (bucket_start_value >= this->dls_rows.size())
	    return;
	for (int lpc = 0; lpc < this->dls_rows[bucket_start_value].size(); lpc++) {
	    label_out.append(this->dls_column_sizes[lpc] - this->dls_rows[bucket_start_value][lpc].length(), ' ');
	    label_out.append(this->dls_rows[bucket_start_value][lpc]);
	}
    };

    void push_column(const char *colstr) {
	int index = this->dls_rows.back().size();
	
	this->dls_rows.back().push_back(colstr);
	if (this->dls_rows.back().size() > this->dls_column_sizes.size()) {
	    this->dls_column_sizes.push_back(1);
	}
	this->dls_column_sizes[index] =
	    std::max(this->dls_column_sizes[index], strlen(colstr) + 1);
    };

    std::vector< std::vector< std::string > > dls_rows;
    std::vector< size_t > dls_column_sizes;
};

#endif
