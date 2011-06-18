
#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <iostream>

#include "pcrepp.hh"
#include "data_scanner.hh"
#include "data_parser.hh"

using namespace std;

const char *TMP_NAME = "scanned.tmp";

int main(int argc, char *argv[])
{
    int c, retval = EXIT_SUCCESS;
    bool prompt = false;
    
    while ((c = getopt(argc, argv, "p")) != -1) {
	switch (c) {
	case 'p':
	    prompt = true;
	    break;
	default:
	    retval = EXIT_FAILURE;
	    break;
	}
    }
    
    argc -= optind;
    argv += optind;

    if (retval != EXIT_SUCCESS) {
    }
    else if (argc != 1) {
	fprintf(stderr, "error: expecting a single file name argument\n");
	retval = EXIT_FAILURE;
    }
    else {
	istream *in;
	string dstname;
	FILE *out;

	if (strcmp(argv[0], "-") == 0) {
	    in = &cin;
	}
	else {
	    ifstream *ifs = new ifstream(argv[0]);
	    
	    if (!ifs->is_open()) {
		fprintf(stderr, "error: unable to open file\n");
		retval = EXIT_FAILURE;
	    }
	    else {
		in = ifs;
	    }
	}

	if (retval == EXIT_FAILURE) {
	}
	else if ((out = fopen(TMP_NAME, "w")) == NULL) {
	    fprintf(stderr, "error: unable to temporary file for writing\n");
	    retval = EXIT_FAILURE;
	}
	else {
	    char cmd[2048];
	    string line;
	    int rc;
	    
	    getline(*in, line);
	    if (strcmp(argv[0], "-") == 0) {
		line = "             " + line;
	    }
	    
	    data_scanner ds(line.substr(13));
	    data_token_t token;
	    
	    data_parser dp(&ds);
	    
	    dp.parse();
	    dp.print(out);
	    fclose(out);

	    sprintf(cmd, "diff -u %s %s", argv[0], TMP_NAME);
	    rc = system(cmd);
	    if (rc != 0) {
		if (prompt) {
		    char resp;
		    
		    printf("Would you like to update the original file? (y/N) ");
		    fflush(stdout);
		    if (scanf("%c", &resp) == 1 && resp == 'y')
			rename(TMP_NAME, argv[0]);
		    else
			retval = EXIT_FAILURE;
		}
		else {
		    fprintf(stderr, "error: mismatch\n");
		    retval = EXIT_FAILURE;
		}
	    }
	}
    }

    return retval;
}
