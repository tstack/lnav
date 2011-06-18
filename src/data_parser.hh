
#ifndef __data_parser_hh
#define __data_parser_hh

#include <stdio.h>

#include <list>
#include <algorithm>

#include "pcrepp.hh"
#include "data_scanner.hh"

template<class ForwardIterator1, class ForwardIterator2, class BinaryPredicate>
ForwardIterator1 find_first_not_of(ForwardIterator1 first, ForwardIterator1 last,
				   ForwardIterator2 s_first, ForwardIterator2 s_last,
				   BinaryPredicate p)
{
    for (; first != last; ++first) {
	bool found = false;
	
        for (ForwardIterator2 it = s_first; it != s_last; ++it) {
            if (p(*first, *it)) {
                found = true;
            }
        }
	if (!found)
	    return first;
    }
    return last;
}

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
	    this->e_sub_elements = NULL;
	};

	~element() {
	    if (this->e_sub_elements != NULL) {
		delete this->e_sub_elements;
		this->e_sub_elements = NULL;
	    }
	};

	void assign_elements(std::list<element> &subs) {
	    if (this->e_sub_elements == NULL)
		this->e_sub_elements = new std::list<element>();
	    this->e_sub_elements->splice(this->e_sub_elements->end(), subs);
	    this->update_capture();
	};

	void update_capture(void) {
	    if (this->e_sub_elements != NULL) {
		this->e_capture.c_begin =
		    this->e_sub_elements->front().e_capture.c_begin;
		this->e_capture.c_end =
		    this->e_sub_elements->back().e_capture.c_end;
	    }
	};

	void print(FILE *out, pcre_input &pi, int offset = 0) {
	    if (this->e_sub_elements != NULL) {
		for (std::list<data_parser::element>::iterator iter2 =
			 this->e_sub_elements->begin();
		     iter2 != this->e_sub_elements->end();
		     ++iter2) {
		    iter2->print(out, pi, offset + 1);
		}
	    }

	    fprintf(out, "%4s %3d:%-3d ",
		    data_scanner::token2name(this->e_token),
		    this->e_capture.c_begin,
		    this->e_capture.c_end);
	    for (int lpc = 0; lpc < this->e_capture.c_end; lpc++) {
		if (lpc == this->e_capture.c_begin)
		    fputc('^', out);
		else if (lpc == (this->e_capture.c_end - 1))
		    fputc('^', out);
		else if (lpc > this->e_capture.c_begin)
		    fputc('-', out);
		else
		    fputc(' ', out);
	    }
	    fputc('\n', out);
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
		       bool repeating = false);

    bool reduceAnyOf(std::list<element> &reduction,
		     const data_token_t *possibilities_start,
		     const data_token_t *possibilities_end) {
	size_t poss_size = (possibilities_end - possibilities_start);
	std::list<element>::iterator iter;
	bool retval = false;

	reduction.clear();

	iter = find_first_not_of(this->dp_stack.begin(),
				 this->dp_stack.end(),
				 possibilities_start, possibilities_end,
				 element_cmp());
	if (iter != this->dp_stack.begin()) {
	    reduction.splice(reduction.end(),
			     this->dp_stack,
			     this->dp_stack.begin(),
			     iter);

	    retval = true;
	}

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

    void reduceQual(const struct element &lookahead);
    void reduceRow(void);
    void reducePair(void);

    void print(FILE *out) {
	fprintf(out, "             %s\n",
		this->dp_scanner->get_input().get_string());
	for (std::list<data_parser::element>::iterator iter = this->dp_stack.begin();
	     iter != this->dp_stack.end();
	     ++iter) {
	    iter->print(out, this->dp_scanner->get_input());
	}
    };
    
    std::list<element> dp_stack;
    
private:
    data_scanner *dp_scanner;
};

#endif
