
#include "config.h"

#include <stdio.h>

#include "pcrepp.hh"
#include "data_scanner.hh"
#include "data_parser.hh"

int main(int argc, char *argv[])
{
    pcre_context_static<30> pc;

    data_scanner ds("a=1 b=2");
    data_scanner ds2("a=1 b=2");
    data_token_t token;
    
    while (ds.tokenize(pc, token)) {
	printf("tok %d  %d:%d\n", token,
	       pc.begin()->c_begin,
	       pc.begin()->c_end);
    }
    
    data_parser dp(&ds2);
    
    dp.parse();
}
