
#ifndef __data_parser_hh
#define __data_parser_hh

#include <list>
#include <algorithm>

#include "pcrepp.hh"
#include "data_scanner.hh"

class data_parser {

public:
    struct element {
	element() : e_token(DT_INVALID) { };
	
	pcre_context::capture_t e_capture;
	data_token_t e_token;
    };

    struct element_cmp {
	bool operator()(data_token_t token, const element &b) const {
	    return token == b.e_token;
	};
    };

    struct element_if {
	element_if(data_token_t token) : ei_token(token) { };

	bool operator()(const element &a) const {
	    return a.e_token == this->ei_token;
	};

    private:
	data_token_t ei_token;
    };
    
    data_parser(data_scanner *ds) : dp_scanner(ds) { };

    void parse(void) {
	pcre_context_static<30> pc;
	struct element elem;
	
	while (this->dp_scanner->tokenize(pc, elem.e_token)) {
	    elem.e_capture = *(pc.begin());
	    
	    this->reduce(elem);
	}
    };

    void reduce(const element &elem);

    bool reducePattern(std::list<element> &reduction,
		       const data_token_t *pattern_start,
		       const data_token_t *pattern_end,
		       bool repeating = false) {
	size_t pattern_size = (pattern_end - pattern_start);
	bool retval = false;

	reduction.clear();
	if (pattern_size <= this->dp_stack.size() &&
	    std::equal(pattern_start, pattern_end,
		       this->dp_stack.begin(),
		       element_cmp())) {
	    std::list<element>::iterator match_end = this->dp_stack.begin();

	    advance(match_end, pattern_size);
	    reduction.splice(reduction.begin(),
			     this->dp_stack,
			     this->dp_stack.begin(),
			     match_end);
	}

	return retval;
    };
    
    std::list<element> dp_stack;
    
private:
    data_scanner *dp_scanner;
};

#endif
