
#ifndef __data_parser_hh
#define __data_parser_hh

#include <list>
#include <algorithm>

#include "pcrepp.hh"
#include "data_scanner.hh"

class data_parser {

public:
    struct element {
	element() : e_token(DT_INVALID), e_sub_elements(NULL) { };
	element(std::list<element> &subs, data_token_t token)
	    : e_capture(subs.front().e_capture.c_begin,
			subs.back().e_capture.c_end),
	      e_token(token),
	      e_sub_elements(NULL) {
	};
	
	element(const element &other) {
	    assert(other.e_sub_elements == NULL);

	    this->e_capture = other.e_capture;
	    this->e_token = other.e_token;
	};

	~element() {
	    if (this->e_sub_elements != NULL) {
		delete this->e_sub_elements;
		this->e_sub_elements = NULL;
	    }
	};

	void assign_elements(std::list<element> &subs) {
	    this->e_sub_elements = new std::list<element>();
	    this->e_sub_elements->splice(this->e_sub_elements->begin(), subs);
	};
	
	pcre_context::capture_t e_capture;
	data_token_t e_token;
	
	std::list<element> *e_sub_elements;
    };

    struct element_cmp {
	bool operator()(data_token_t token, const element &elem) const {
	    return token == elem.e_token || token == DT_ANY;
	};
	
	bool operator()(const element &elem, data_token_t token) const {
	    return (*this)(token, elem);
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
	bool found, retval = false;

	reduction.clear();

	do {
	    found = false;
	    if (pattern_size <= this->dp_stack.size() &&
		std::equal(pattern_start, pattern_end,
			   this->dp_stack.begin(),
			   element_cmp())) {
		std::list<element>::iterator match_end = this->dp_stack.begin();
		
		advance(match_end, pattern_size);
		reduction.splice(reduction.end(),
				 this->dp_stack,
				 this->dp_stack.begin(),
				 match_end);

		retval = found = true;
	    }
	} while (found && repeating);

	reduction.reverse();
	
	return retval;
    };

    bool reduceUpTo(std::list<element> &reduction,
		    const data_token_t *possibilities_start,
		    const data_token_t *possibilities_end) {
	size_t poss_size = (possibilities_end - possibilities_start);
	std::list<element>::iterator iter;
	bool retval = false;

	reduction.clear();

	iter = std::find_first_of(this->dp_stack.begin(), this->dp_stack.end(),
				  possibilities_start, possibilities_end,
				  element_cmp());
	if (iter != this->dp_stack.end()) {
	    reduction.splice(reduction.end(),
			     this->dp_stack,
			     this->dp_stack.begin(),
			     iter);

	    retval = true;
	}

	reduction.reverse();
	
	return retval;
    };

    void reduceAggregate(void);
    void reducePair(void);

    void print(void) {
	for (std::list<data_parser::element>::iterator iter = this->dp_stack.begin();
	     iter != this->dp_stack.end();
	     ++iter) {
	    printf("%d %d:%d %s\n",
		   iter->e_token,
		   iter->e_capture.c_begin,
		   iter->e_capture.c_end,
		   this->dp_scanner->get_input().get_substr(&iter->e_capture).c_str());
	    if (iter->e_sub_elements != NULL) {
		for (std::list<data_parser::element>::iterator iter2 =
			 iter->e_sub_elements->begin();
		     iter2 != iter->e_sub_elements->end();
		     ++iter2) {
		    printf("  %d %d:%d %s\n",
			   iter2->e_token,
			   iter2->e_capture.c_begin,
			   iter2->e_capture.c_end,
			   this->dp_scanner->get_input().get_substr(&iter2->e_capture).c_str());
		}
	    }
	}
    };
    
    std::list<element> dp_stack;
    
private:
    data_scanner *dp_scanner;
};

#endif
