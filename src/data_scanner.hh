
#ifndef __data_scanner_hh
#define __data_scanner_hh

#include <string>

#include "pcrepp.hh"

enum data_token_t {
    DT_URL,
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
};

class data_scanner {
public:
    data_scanner(const std::string &line) :
	ds_line(line),
	ds_pcre_input(line) {
    };

    bool tokenize(pcre_context &pc, data_token_t &token_out);

private:
    std::string ds_line;
    pcre_input ds_pcre_input;
};

#endif
