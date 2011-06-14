
#include "config.h"

#include <stdio.h>

#include "pcrepp.hh"
#include "data_scanner.hh"
#include "data_parser.hh"

using namespace std;

int main(int argc, char *argv[])
{
    pcre_context_static<30> pc;

    data_scanner ds("a=1 b=2\n");
    //data_scanner ds2("a=1 b=2  c=3,4\n");
    data_scanner ds2("c=3,4\n");
    data_token_t token;
    
    while (ds.tokenize(pc, token)) {
	printf("tok %d  %d:%d\n", token,
	       pc.begin()->c_begin,
	       pc.begin()->c_end);
    }
    
    data_parser dp(&ds2);
    
    dp.parse();

    printf("done\n");
    for (list<data_parser::element>::iterator iter = dp.dp_stack.begin();
	 iter != dp.dp_stack.end();
	 ++iter) {
	printf("%d %d:%d %s\n",
	       iter->e_token,
	       iter->e_capture.c_begin,
	       iter->e_capture.c_end,
	       ds2.get_input().get_substr(&iter->e_capture).c_str());
	if (iter->e_sub_elements != NULL) {
	    for (list<data_parser::element>::iterator iter2 =
		     iter->e_sub_elements->begin();
		 iter2 != iter->e_sub_elements->end();
		 ++iter2) {
		printf("  %d %d:%d %s\n",
		       iter2->e_token,
		       iter2->e_capture.c_begin,
		       iter2->e_capture.c_end,
		       ds2.get_input().get_substr(&iter2->e_capture).c_str());
	    }
	}
    }
}
