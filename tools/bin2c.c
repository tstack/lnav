// bin2c.c
//
// convert a binary file into a C source vector
//
// THE "BEER-WARE LICENSE" (Revision 3.1415):
// sandro AT sigala DOT it wrote this file. As long as you retain this notice
// you can do whatever you want with this stuff.  If we meet some day, and you
// think this stuff is worth it, you can buy me a beer in return.  Sandro Sigala

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif

const char* name = NULL;

static const char* HEADER_FMT
    = "#ifndef bin2c_%s_h\n"
      "#define bin2c_%s_h\n"
      "\n"
      "#include \"bin2c.hh\"\n"
      "\n"
      "extern struct bin_src_file %s%s;\n"
      "\n"
      "#endif\n"
      "\n";

struct file_meta {
    const char* fm_name;
    unsigned int fm_compressed_size;
    unsigned int fm_size;
};

void
symname(char* dst, const char* fname)
{
    strcpy(dst, fname);
    for (int lpc = 0; dst[lpc]; lpc++) {
        if (!isalnum(dst[lpc])) {
            dst[lpc] = '_';
        }
    }
}

void
process(struct file_meta* fm, FILE* ofile)
{
    struct stat st;

    if (stat(fm->fm_name, &st) == -1) {
        perror("unable to stat file");
        exit(1);
    }

    unsigned char* buf = malloc(st.st_size);
    unsigned char* dest = malloc(st.st_size + 1024);

    int fd = open(fm->fm_name, O_RDONLY);
    if (fd == -1) {
        perror("unable to open file");
        exit(1);
    }

    int rc;
    while ((rc = read(fd, &buf[fm->fm_size], (st.st_size - fm->fm_size))) > 0) {
        fm->fm_size += rc;
    }

    uLongf destLen = st.st_size + 1024;
    int cres = compress(dest, &destLen, buf, st.st_size);
    assert(cres == Z_OK);
    fm->fm_compressed_size = destLen;

    int c, col = 1;
    char sym[1024];

    symname(sym, basename((char*) fm->fm_name));
    fprintf(ofile, "static const unsigned char %s_data[] = {\n", sym);
    for (int lpc = 0; lpc < destLen; lpc++) {
        c = dest[lpc];
        if (col >= 78 - 6) {
            fputc('\n', ofile);
            col = 1;
        }
        fprintf(ofile, "0x%.2x, ", c);
        col += 6;
    }
    fprintf(ofile, "0x00\n");
    fprintf(ofile, "\n};\n");

    free(buf);
    free(dest);
}

void
usage()
{
    fprintf(stderr, "usage: bin2c [-n name] <output_file> [input_file1 ...]\n");
    exit(1);
}

int
main(int argc, char** argv)
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

    const char* out_base_name = argv[0];
    char hname[PATH_MAX], hname_tmp[PATH_MAX], cname[PATH_MAX];

    argc -= 1;
    argv += 1;

    snprintf(hname, sizeof(hname), "%s.h", out_base_name);
    snprintf(hname_tmp, sizeof(hname_tmp), "%s.tmp", hname);

    FILE* hfile = fopen(hname_tmp, "w+b");
    if (hfile == NULL) {
        fprintf(stderr, "cannot open %s for writing\n", hname_tmp);
        exit(1);
    }

    snprintf(cname, sizeof(cname), "%s.cc", out_base_name);
    FILE* cfile = fopen(cname, "wb");
    if (cfile == NULL) {
        fprintf(stderr, "cannot open %s for writing\n", cname);
        exit(1);
    }

    char sym[1024];
    if (name) {
        strcpy(sym, name);
    } else {
        const char* in_base = basename(argv[0]);

        symname(sym, in_base);
    }

    int array = argc > 1 || name;
    char trailer[16];

    if (array) {
        snprintf(trailer, sizeof(trailer), "[%d]", argc);
    } else {
        trailer[0] = '\0';
    }
    fprintf(hfile, HEADER_FMT, sym, sym, sym, trailer);
    fflush(hfile);
    rewind(hfile);

    int same = 1;
    {
        FILE* orig_hfile = fopen(hname, "rb");
        if (orig_hfile == NULL) {
            same = 0;
        } else {
            while (1) {
                char orig_line[1024], new_line[1024];

                char* orig_res
                    = fgets(orig_line, sizeof(orig_line), orig_hfile);
                char* new_res = fgets(new_line, sizeof(new_line), hfile);

                if (orig_res == NULL && new_res == NULL) {
                    break;
                }

                if (orig_res == NULL || new_res == NULL) {
                    same = 0;
                    break;
                }

                if (strcmp(orig_line, new_line) != 0) {
                    same = 0;
                    break;
                }
            }
        }
    }
    fclose(hfile);
    if (!same) {
        rename(hname_tmp, hname);
    } else {
        remove(hname_tmp);
    }

    fprintf(cfile, "#include \"bin2c.hh\"\n");
    fprintf(cfile, "\n");

    struct file_meta* meta = malloc(sizeof(struct file_meta) * argc);

    memset(meta, 0, sizeof(struct file_meta) * argc);
    for (int lpc = 0; lpc < argc; lpc++) {
        meta[lpc].fm_name = argv[lpc];
        process(&meta[lpc], cfile);
    }

    fprintf(cfile, "struct bin_src_file %s%s = {\n", sym, trailer);
    for (int lpc = 0; lpc < argc; lpc++) {
        char sym[1024];

        symname(sym, basename((char*) meta[lpc].fm_name));
        fprintf(cfile, "    ");
        if (array) {
            fprintf(cfile, "{ ");
        }
        fprintf(cfile,
                "\"%s\", %s_data, %d, %d",
                basename((char*) meta[lpc].fm_name),
                sym,
                meta[lpc].fm_compressed_size,
                meta[lpc].fm_size);
        if (array) {
            fprintf(cfile, " },");
        }
        fprintf(cfile, "\n");
    }
    fprintf(cfile, "};\n");
    fclose(cfile);
    free(meta);

    return 0;
}
