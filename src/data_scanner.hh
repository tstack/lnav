
#ifndef __data_scanner_hh
#define __data_scanner_hh

#include <string>

#include "pcrepp.hh"

enum data_token_t {
    DT_INVALID = -1,
    
    DT_URL = 0,
    DT_SEPARATOR,
    DT_COMMA,

    DT_MAC_ADDRESS,
    DT_IP_ADDRESS,

    DT_TIME,
    DT_HEX_NUMBER,
    DT_OCTAL_NUMBER,
    DT_PERCENTAGE,
    DT_NUMBER,

    DT_STRING,
    DT_LINE,
    DT_WHITE,
    DT_DOT,

    DT_GARBAGE,

    DT_TERMINAL_MAX = DT_GARBAGE + 1,

    DNT_KEY = 50,
    DNT_PAIR,
    DNT_VALUE,
    DNT_AGGREGATE,
    DNT_ROW,
    DNT_UNITS,
    DNT_MEASUREMENT,
    DNT_VARIABLE_KEY,
    DNT_ROWRANGE,
	
    T_ANY = 100,
};

class data_scanner {
public:
    data_scanner(const std::string &line) :
	ds_line(line),
	ds_pcre_input(ds_line.c_str()) {
    };

    bool tokenize(pcre_context &pc, data_token_t &token_out);

private:
    std::string ds_line;
    pcre_input ds_pcre_input;
};

#endif
