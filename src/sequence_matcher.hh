
#ifndef __sequence_matcher_hh
#define __sequence_matcher_hh

#include <list>
#include <string>
#include <vector>

#include "byte_array.hh"

class sequence_matcher {
public:
    typedef std::vector<std::string> field_row_t;
    typedef std::list<field_row_t> field_col_t;

    typedef byte_array<20> id_t;
    
    enum field_type_t {
	FT_VARIABLE,
	FT_CONSTANT,
    };

    struct field {
	
    public:
	field() : sf_type(FT_VARIABLE) { };

	field_type_t sf_type;
	field_row_t sf_value;
    };

    sequence_matcher(field_col_t &example);

    void identity(const std::vector<std::string> &values, id_t &id_out);

    template<typename T>
    bool match(const std::vector<std::string> &values,
	       std::vector<T> &state,
	       T index) {
	bool index_match = true;
	int lpc = 0;
	
    retry:
	for (std::list<field>::iterator iter = this->sm_fields.begin();
	     iter != this->sm_fields.end();
	     ++iter, lpc++) {
	    if (iter->sf_type != sequence_matcher::FT_CONSTANT) {
		continue;
	    }

	    if (iter->sf_value[state.size()] != values[lpc]) {
		if (state.size() > 0) {
		    state.clear();
		    lpc = 0;
		    goto retry;
		}
		else {
		    index_match = false;
		    break;
		}
	    }
	}
	
	if (index_match) {
	    state.push_back(index);
	}
	
	return (this->sm_count == state.size());
    };

private:
    int sm_count;
    std::list<field> sm_fields;
};

#endif
