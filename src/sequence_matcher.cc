
#include "config.h"

#include <openssl/sha.h>

#include "sequence_matcher.hh"

using namespace std;

sequence_matcher::sequence_matcher(field_col_t &example)
{
    for (field_col_t::iterator col_iter = example.begin();
	 col_iter != example.end();
	 ++col_iter) {
	std::string first_value;
	field sf;
	
	    sf.sf_value = *col_iter;
	    for (field_row_t::iterator row_iter = (*col_iter).begin();
		 row_iter != (*col_iter).end();
		 ++row_iter) {
		if (row_iter == (*col_iter).begin()) {
		    first_value = *row_iter;
		}
		else if (first_value != *row_iter) {
		    sf.sf_type = FT_CONSTANT;
		}
	    }
	    if (sf.sf_type == FT_VARIABLE)
		sf.sf_value.clear();
	    this->sm_fields.push_back(sf);
	}
	this->sm_count = example.front().size();
}

void sequence_matcher::identity(const std::vector<string> &values, id_t &id_out)
{
    SHA_CTX context;
    int lpc = 0;
    
    SHA_Init(&context);
    for (std::list<field>::iterator iter = sm_fields.begin();
	 iter != sm_fields.end();
	 ++iter, lpc++) {
	if (iter->sf_type == FT_VARIABLE) {
	    SHA_Update(&context,
		       values[lpc].c_str(),
		       values[lpc].length() + 1);
	}
    }
    SHA_Final(id_out.ba_data, &context);
}
