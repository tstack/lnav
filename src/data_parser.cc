
#include "config.h"

#include "data_parser.hh"

using namespace std;

static data_token_t PATTERN_KEY[] = {
    DT_STRING,
};

static data_token_t UPTO_SEPARATOR[] = {
    DT_SEPARATOR,
    DT_LINE,
};

static data_token_t PATTERN_PAIR[] = {
    DNT_ROW,
    DT_SEPARATOR,
    DNT_KEY,
};

static data_token_t PATTERN_AGGREGATE[] = {
    DT_ANY,
    DT_COMMA,
    DNT_AGGREGATE,
};

void data_parser::reduceAggregate(void)
{
    std::list<element> reduction;

    if (this->reducePattern(reduction,
			    PATTERN_AGGREGATE,
			    PATTERN_AGGREGATE +
			    sizeof(PATTERN_AGGREGATE) / sizeof(data_token_t))) {
	struct element &top = this->dp_stack.front();
	
	this->dp_stack.push_front(element(reduction, DNT_AGGREGATE));
	top.assign_elements(*reduction.front().e_sub_elements);
	if (reduction.back().e_sub_elements != NULL)
	    top.assign_elements(*reduction.back().e_sub_elements);
	else
	    top.e_sub_elements->push_back(reduction.back());
    }
}

void data_parser::reducePair(void)
{
    std::list<element> reduction;

    this->reduceAggregate();
    if (this->reduceUpTo(reduction,
			 UPTO_SEPARATOR,
			 UPTO_SEPARATOR +
			 sizeof(UPTO_SEPARATOR) / sizeof(data_token_t))) {
	this->dp_stack.push_front(element(reduction, DNT_ROW));
	this->dp_stack.front().assign_elements(reduction);
    }
    
    if (this->reducePattern(reduction,
			    PATTERN_PAIR,
			    PATTERN_PAIR +
			    sizeof(PATTERN_PAIR) / sizeof(data_token_t))) {
	std::list<element>::iterator middle = reduction.begin();
	
	++middle;
	reduction.erase(middle);
	this->dp_stack.push_front(element(reduction, DNT_PAIR));
	this->dp_stack.front().assign_elements(reduction);
    }
}

void data_parser::reduce(const element &lookahead)
{
    std::list<element> reduction;
    bool push_lookahead = true;

    switch (lookahead.e_token) {
    case DT_INVALID:
    case DT_WHITE:
	push_lookahead = false;
	break;

    case DT_LINE:
	this->reducePair();
	push_lookahead = false;
	break;

    case DT_COMMA:
	this->reduceAggregate();
	if (!this->dp_stack.empty() &&
	    this->dp_stack.front().e_token != DNT_AGGREGATE) {
	    if (this->dp_stack.front().e_token == DT_SEPARATOR) {
		push_lookahead = false;
	    }
	    else {
		std::list<element>::iterator next_elem = this->dp_stack.begin();

		advance(next_elem, 1);
		reduction.splice(reduction.end(),
				 this->dp_stack,
				 this->dp_stack.begin(),
				 next_elem);
		this->dp_stack.push_front(element(reduction, DNT_AGGREGATE));
		this->dp_stack.front().assign_elements(reduction);
	    }
	}
	break;
	
    case DT_SEPARATOR:
	if (this->reducePattern(reduction,
				PATTERN_KEY,
				PATTERN_KEY +
				sizeof(PATTERN_KEY) / sizeof(data_token_t),
				true)) {
	    this->reducePair();
	    this->dp_stack.push_front(element(reduction, DNT_KEY));
	}
	break;
    }

    if (push_lookahead) {
	this->dp_stack.push_front(lookahead);
    }
    
    // this->print();
    printf("----\n");
}
