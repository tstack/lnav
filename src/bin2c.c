// bin2c.c
//
// convert a binary file into a C source vector
//
// THE "BEER-WARE LICENSE" (Revision 3.1415):
// sandro AT sigala DOT it wrote this file. As long as you retain this notice you can do
// whatever you want with this stuff.  If we meet some day, and you think this stuff is
// worth it, you can buy me a beer in return.  Sandro Sigala

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <libgen.h>

#include "bin2c.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

const char *name = NULL;

static const char *HEADER_FMT =
    "#ifndef bin2c_%s_h\n"
    "#define bin2c_%s_h\n"
    "\n"
    "#include \"bin2c.h\"\n"
    "\n"
    "extern \"C\" {\n"
    "extern struct bin_src_file %s%s;\n"
    "}\n"
    "\n"
    "#endif\n"
    "\n";

void symname(char *dst, const char *fname)
{
    strcpy(dst, fname);
    for (int lpc = 0; dst[lpc]; lpc++) {
        if (!isalnum(dst[lpc])) {
            dst[lpc] = '_';
        }
    }
}

void process(struct bin_src_file *bsf, FILE *ofile)
{
	FILE *ifile;

	ifile = fopen(bsf->bsf_name, "rb");
	if (ifile == NULL)
	{
		fprintf(stderr, "cannot open %s for reading\n", bsf->bsf_name);
		exit(1);
	}

	int c, col = 1;
	char sym[1024];

	symname(sym, basename((char *) bsf->bsf_name));
	fprintf(ofile, "static const unsigned char %s_data[] = {\n", sym);
	while ((c = fgetc(ifile)) != EOF)
	{
		if (col >= 78 - 6)
		{
			fputc('\n', ofile);
			col = 1;
		}
		fprintf(ofile, "0x%.2x, ", c);
		col += 6;
        bsf->bsf_size += 1;
	}
	fprintf(ofile, "0x00\n");
	fprintf(ofile, "\n};\n");

	fclose(ifile);
}

void usage(void)
{
	fprintf(stderr, "usage: bin2c [-n name] <output_file> [input_file1 ...]\n");
	exit(1);
}

int main(int argc, char **argv)
{
    int c;

    while ((c = getopt(argc, argv, "hn:")) != -1) {
        switch (c) {
            case 'n':
                name = optarg;
                break;
            default:
                usage();
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (argc < 1) {
        usage();
    }

    const char *out_base_name = argv[0];
    char hname[PATH_MAX], cname[PATH_MAX];

    argc -= 1;
    argv += 1;

    snprintf(hname, sizeof(hname), "%s.h", out_base_name);

    FILE *hfile = fopen(hname, "wb");
    if (hfile == NULL)
    {
        fprintf(stderr, "cannot open %s for writing\n", hname);
        exit(1);
    }

    snprintf(cname, sizeof(cname), "%s.c", out_base_name);
    FILE *cfile = fopen(cname, "wb");
    if (cfile == NULL)
    {
        fprintf(stderr, "cannot open %s for writing\n", cname);
        exit(1);
    }

    char sym[1024];
    if (name) {
        strcpy(sym, name);
    } else {
        const char *in_base = basename(argv[0]);

        symname(sym, in_base);
    }

    int array = argc > 1 || name;
    fprintf(hfile, HEADER_FMT,
        sym,
        sym,
        sym,
        array ? "[]" : "");
    fclose(hfile);

    fprintf(cfile, "#include \"bin2c.h\"\n");
    fprintf(cfile, "\n");

    struct bin_src_file *meta = alloca(sizeof(struct bin_src_file) * argc);

    memset(meta, 0, sizeof(struct bin_src_file) * argc);
    for (int lpc = 0; lpc < argc; lpc++) {
        meta[lpc].bsf_name = argv[lpc];
        process(&meta[lpc], cfile);
    }

    fprintf(cfile, "struct bin_src_file %s%s = {\n", sym, array ? "[]" : "");
    for (int lpc = 0; lpc < argc; lpc++) {
        char sym[1024];

        symname(sym, basename((char *) meta[lpc].bsf_name));
        fprintf(cfile, "    ");
        if (array) {
            fprintf(cfile, "{ ");
        }
        fprintf(cfile, "\"%s\", %s_data, %d",
            basename((char *) meta[lpc].bsf_name), sym, meta[lpc].bsf_size);
        if (array) {
            fprintf(cfile, " },");
        }
        fprintf(cfile, "\n");
    }
    if (array) {
        fprintf(cfile, "    { 0 }\n");
    }
    fprintf(cfile, "};\n");
    fclose(cfile);

    return 0;
}
