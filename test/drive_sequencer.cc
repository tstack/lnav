
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <openssl/sha.h>

#include <map>
#include <list>
#include <vector>
#include <algorithm>

#include "pcrepp.hh"
#include "logfile.hh"

using namespace std;

string in[] = {
    "eth0 up",
    "eth0 down",
    ""
};

class sequence_source {
public:
    virtual ~sequence_source() { };

    virtual bool sequence_value_for_field(int line,
					  int col,
					  std::string &value_out) = 0;
};

template<size_t BYTE_COUNT>
class byte_array {
public:

    bool operator<(const byte_array &other) const {
	return memcmp(this->ba_data, other.ba_data, BYTE_COUNT) < 0;
    };
    
    unsigned char ba_data[BYTE_COUNT];
};

class sequence_matcher {
public:
    typedef std::vector<string> field_row_t;
    typedef std::list<field_row_t> field_col_t;
    
    enum field_type_t {
	FT_VARIABLE,
	FT_CONSTANT,
    };

    class state<T> {
    public:
	std::vector<T> sms_line;

	byte_array<20> sms_id;
    };

    class field {
	
    public:
	field() : sf_type(FT_VARIABLE) { };

	field_type_t sf_type;
	field_col_t sf_value;
    };

    sequence_matcher(field_col_t example) {
	for (field_col_t::iterator col_iter = example.begin();
	     col_iter != example.end();
	     ++col_iter) {
	    std::string first_value;
	    
	    for (field_row_t::iterator row_iter = (*col_iter).begin();
		 row_iter != (*col_iter).end();
		 ++row_iter) {
		if (row_iter == (*col_iter).begin()) {
		    first_value = *row_iter;
		}
		else if (first_value != *row_iter) {
		    
		}
	    }
	}
    };

    
};

int main(int argc, char *argv[])
{
  int c, retval = EXIT_SUCCESS;

  pcre_context_static<20> *captures = new pcre_context_static<20>[2];
  long cols = 0;

  sequence::field *fields = new sequence::field[2];
  
  pcrepp re("(\\w+) (up|down)");

  for (int lpc = 0; in[lpc] != ""; lpc++) {
      pcre_input pi(in[lpc]);
      bool rc;
      
      rc = re.match(captures[lpc], pi);
      cols = max(cols, captures[lpc].end() - captures[lpc].begin());

      assert(rc);
  }

  for (int curr_col = 0; curr_col < cols; curr_col++) {
      string first_row;
      
      for (int curr_row = 0; curr_row < 2; curr_row++) {
	  pcre_input pi(in[curr_row]);
	  string curr = pi.get_substr(captures[curr_col].begin() + curr_col);
	  
	  if (curr_row == 0) {
	      first_row = curr;
	  }
	  else if (first_row != curr) {
	      fields[curr_col].sf_type = sequence::FT_CONSTANT;
	  }
      }
  }

  for (int lpc = 0; lpc < cols; lpc++) {
      printf("field[%d] = %d\n", lpc, fields[lpc].sf_type);
  }
  
  return retval;
}
