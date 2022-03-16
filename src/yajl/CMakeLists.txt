add_library(yajl STATIC
        api/yajl_common.h
        api/yajl_gen.h
        api/yajl_parse.h
        api/yajl_tree.h

        yajl_common.h
        yajl_alloc.h
        yajl_buf.h
        yajl_bytestack.h
        yajl_encode.h
        yajl_lex.h
        yajl_parser.h
        yajl_version.h

        yajl.c
        yajl_alloc.c
        yajl_buf.c
        yajl_encode.c
        yajl_gen.c
        yajl_lex.c
        yajl_parser.c
        yajl_tree.c
        yajl_version.c
        )
target_include_directories(yajl PUBLIC api)
target_include_directories(yajl PRIVATE ..)
