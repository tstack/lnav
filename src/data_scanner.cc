
#include "config.h"

#include "pcrepp.hh"
#include "data_scanner.hh"

using namespace std;

static pcrepp MATCHERS[DT_TERMINAL_MAX] = {
    pcrepp("^([\\w]+://[\\S'\"\\[\\](){}]+)"),
    pcrepp("^(:|=)"),
    pcrepp("^(,|/)"),

    pcrepp("^([0-9a-fA-F][0-9a-fA-F](?::[0-9a-fA-F][0-9a-fA-F]){5,5})"),
    pcrepp("^(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3})"),

    pcrepp("^(\\d?\\d:\\d\\d(:\\d\\d)?(:\\d+|.\\d+)?)"),
    pcrepp("^(-?0x[0-9a-fA-F]+)"),
    pcrepp("^(-?0[0-7]+)"),
    pcrepp("^(-?[0-9]+(\\.[0-9]+)?[ ]*%)"),
    pcrepp("^(-?[0-9]+(\\.[0-9]+)?([eE][-+][0-9]+)?)"),
		
    pcrepp("^([^\\s:=,/(){}\\[\\]]+)"),
    pcrepp("^(\r?\n|\r)"),
    pcrepp("^([ \r\t]+)"),
    pcrepp("^\\."),
    
    pcrepp("^(.)"),
};

bool data_scanner::tokenize(pcre_context &pc, data_token_t &token_out)
{
    int lpc;

    token_out = data_token_t(-1);
    for (lpc = 0; lpc < DT_TERMINAL_MAX; lpc++) {
	if (MATCHERS[lpc].match(pc, this->ds_pcre_input)) {
	    token_out = data_token_t(lpc);
	    break;
	}
    }

    this->ds_pcre_input.ratchet();

    return (token_out != -1);
}
