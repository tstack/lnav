
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

void data_parser::reduce(const element &lookahead)
{
    struct element &top_elem = this->dp_stack.front();

    switch (lookahead.e_token) {
    case DT_INVALID:
	break;

    case DT_WHITE:
	break;
	
    case DT_SEPARATOR:
	
	break;
    }
}
