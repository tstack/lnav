
include(CheckTypeSize)
include(CheckIncludeFile)
include(CheckLibraryExists)
include(CheckFunctionExists)

check_type_size(off_t SIZEOF_OFF_T)

check_include_file("pty.h" HAVE_PTY_H)
check_include_file("util.h" HAVE_UTIL_H)

set(VCS_PACKAGE_STRING "test")

configure_file(config.cmake.h.in config.h)

add_executable(bin2c bin2c.hh bin2c.c)
target_link_libraries(bin2c ZLIB::zlib)

add_executable(ptimec ptimec.hh ptimec.c)

set(TIME_FORMATS
        "@%@"
        "%Y-%m-%d %H:%M:%S"
        "%Y-%m-%d %H:%M:%S%z"
        "%Y-%m-%d %H:%M:%S %z"
        "%Y-%m-%d %H:%M"
        "%Y-%m-%dT%H:%M:%S.%f%z"
        "%y-%m-%dT%H:%M:%S.%f%z"
        "%Y-%m-%dT%H:%M:%SZ"
        "%Y-%m-%dT%H:%M:%S%z"
        "%Y-%m-%dT%H:%M:%S"
        "%Y-%m-%dT%H:%M:%S%z"
        "%Y/%m/%d %H:%M:%S"
        "%Y/%m/%d %H:%M:%S %z"
        "%Y/%m/%d %H:%M:%S%z"
        "%Y/%m/%d %H:%M"
        "%Y %b %d %a %H:%M:%S.%L"
        "%Y %b %d %H:%M:%S.%L"
        "%Y %b %d %H:%M:%S"
        "%a %b %d %H:%M:%S %Y"
        "%a %b %d %H:%M:%S.%f %Y"
        "%a %b %d %H:%M:%S %Z %Y"
        "%a %b %d %H:%M:%S "
        "%a %b %d %H:%M:%S.%L "
        "%d/%b/%Y:%H:%M:%S +0000"
        "%d/%b/%Y:%H:%M:%S %z"
        "%d-%b-%Y %H:%M:%S %z"
        "%d-%b-%Y %H:%M:%S %Z"
        "%d %b %Y %H:%M:%S"
        "%d %b %Y %H:%M:%S.%L"
        "%d %b %Y %H:%M:%S,%L"
        "%b %d %H:%M:%S"
        "%b %d %k:%M:%S"
        "%b %d %l:%M:%S"
        "%b %e, %Y %l:%M:%S %p"
        "%m/%d/%y %H:%M:%S"
        "%m/%d/%Y %I:%M:%S:%L %p %Z"
        "%m/%d/%Y %I:%M:%S %p %Z"
        "%m/%d/%Y %l:%M:%S %p %Z"
        "%m/%e/%Y %I:%M:%S %p"
        "%m/%e/%Y %l:%M:%S %p"
        "%d/%b/%y %H:%M:%S"
        "%m%d %H:%M:%S"
        "%H:%M:%S"
        "%M:%S"
        "%m/%d %H:%M:%S"
        "%Y-%m-%d"
        "%Y-%m"
        "%Y/%m/%d"
        "%Y/%m"
        "%s.%f"
        )

set(GEN_SRCS "")

add_custom_command(
        OUTPUT time_fmts.cc
        COMMAND ptimec ${TIME_FORMATS} > time_fmts.cc
)
list(APPEND GEN_SRCS time_fmts.cc)

function(bin2c)
    cmake_parse_arguments(
            BIN2C_
            ""
            "VARNAME"
            ""
            ${ARGN}
    )

    list(TRANSFORM BIN2C_UNPARSED_ARGUMENTS "\\." "-")
    add_custom_command(
            OUTPUT "${DST_FILE}.h" "${DST_FILE}.cc"
            COMMAND bin2c "${DST_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/${FILE_TO_LINK}"
            DEPENDS bin2c "${FILE_TO_LINK}"
    )
endfunction(bin2c)

foreach(FILE_TO_LINK
        ansi-palette.json
        xterm-palette.json
        help.txt
        init.sql)
    string(REPLACE "." "-" DST_FILE "${FILE_TO_LINK}")
    add_custom_command(
            OUTPUT "${DST_FILE}.h" "${DST_FILE}.cc"
            COMMAND bin2c "${DST_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/${FILE_TO_LINK}"
            DEPENDS bin2c "${FILE_TO_LINK}"
    )
    list(
            APPEND GEN_SRCS
            "${CMAKE_CURRENT_BINARY_DIR}/${DST_FILE}.h"
            "${CMAKE_CURRENT_BINARY_DIR}/${DST_FILE}.cc"
    )
endforeach(FILE_TO_LINK)

set(FORMAT_FILES
        formats/access_log.json
        formats/alb_log.json
        formats/autodeploy_log.json
        formats/block_log.json
        formats/candlepin_log.json
        formats/choose_repo_log.json
        formats/cups_log.json
        formats/dpkg_log.json
        formats/elb_log.json
        formats/engine_log.json
        formats/error_log.json
        formats/fsck_hfs_log.json
        formats/glog_log.json
        formats/haproxy_log.json
        formats/java_log.json
        formats/journald_json_log.json
        formats/katello_log.json
        formats/openam_log.json
        formats/openamdb_log.json
        formats/openstack_log.json
        formats/page_log.json
        formats/papertrail_log.json
        formats/snaplogic_log.json
        formats/sssd_log.json
        formats/strace_log.json
        formats/sudo_log.json
        formats/syslog_log.json
        formats/s3_log.json
        formats/tcf_log.json
        formats/tcsh_history.json
        formats/uwsgi_log.json
        formats/vdsm_log.json
        formats/vmk_log.json
        formats/vmw_log.json
        formats/xmlrpc_log.json
        )

set(FORMAT_FILE_PATHS ${FORMAT_FILES})

list(TRANSFORM FORMAT_FILE_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_custom_command(
        OUTPUT default-formats.h default-formats.cc
        COMMAND bin2c -n lnav_format_json default-formats ${FORMAT_FILE_PATHS}
        DEPENDS bin2c ${FORMAT_FILES}
)
list(APPEND GEN_SRCS default-formats.h default-formats.cc)

set(CONFIG_FILES
        root-config.json
        keymaps/de-keymap.json
        keymaps/default-keymap.json
        keymaps/fr-keymap.json
        keymaps/uk-keymap.json
        keymaps/us-keymap.json
        themes/default-theme.json
        themes/grayscale.json
        themes/eldar.json
        themes/monocai.json
        themes/night-owl.json
        themes/solarized-dark.json
        themes/solarized-light.json
        )

set(CONFIG_FILE_PATHS ${CONFIG_FILES})

list(TRANSFORM CONFIG_FILE_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_custom_command(
        OUTPUT default-config.h default-config.cc
        COMMAND bin2c -n lnav_config_json default-config ${CONFIG_FILE_PATHS}
        DEPENDS bin2c ${CONFIG_FILES}
)
list(APPEND GEN_SRCS default-config.h default-config.cc)


set(BUILTIN_LNAV_SCRIPTS
        scripts/dhclient-summary.lnav
        scripts/lnav-pop-view.lnav
        scripts/partition-by-boot.lnav
        scripts/rename-stdin.lnav
        scripts/search-for.lnav)

set(BUILTIN_LNAV_SCRIPT_PATHS ${BUILTIN_LNAV_SCRIPTS})

list(TRANSFORM BUILTIN_LNAV_SCRIPT_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_custom_command(
        OUTPUT builtin-scripts.h builtin-scripts.cc
        COMMAND bin2c -n lnav_scripts builtin-scripts ${BUILTIN_LNAV_SCRIPT_PATHS}
        DEPENDS bin2c ${BUILTIN_LNAV_SCRIPTS}
)
list(APPEND GEN_SRCS builtin-scripts.h builtin-scripts.cc)

set(BUILTIN_SH_SCRIPTS
        scripts/dhclient-summary.lnav
        scripts/lnav-pop-view.lnav
        scripts/partition-by-boot.lnav
        scripts/search-for.lnav)

set(BUILTIN_SH_SCRIPT_PATHS ${BUILTIN_SH_SCRIPTS})

list(TRANSFORM BUILTIN_SH_SCRIPT_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_custom_command(
        OUTPUT builtin-sh-scripts.h builtin-sh-scripts.cc
        COMMAND bin2c -n lnav_sh_scripts builtin-sh-scripts ${BUILTIN_SH_SCRIPT_PATHS}
        DEPENDS bin2c ${BUILTIN_SH_SCRIPTS}
)
list(APPEND GEN_SRCS builtin-sh-scripts.h builtin-sh-scripts.cc)

add_library(diag STATIC
        ${GEN_SRCS}

        config.h

        all_logs_vtab.cc
        ansi_scrubber.cc
        archive_manager.cc
        attr_line.cc
        base/isc.cc
        bin2c.hh
        bookmarks.cc
        bottom_status_source.cc
        collation-functions.cc
        column_namer.cc
        command_executor.cc
        curl_looper.cc
        base/date_time_scanner.cc
        db_sub_source.cc
        elem_to_json.cc
        environ_vtab.cc
        extension-functions.cc
        field_overlay_source.cc
        file_collection.cc
        file_format.cc
        file_vtab.cc
        files_sub_source.cc
        filter_observer.cc
        filter_status_source.cc
        filter_sub_source.cc
        fs-extension-functions.cc
        fstat_vtab.cc
        fts_fuzzy_match.cc
        grep_proc.cc
        help_text.cc
        help_text_formatter.cc
        highlighter.cc
        hist_source.cc
        hotkeys.cc
        base/humanize.cc
        input_dispatcher.cc
        base/intern_string.cc
        base/is_utf8.cc
        json-extension-functions.cc
        yajlpp/json_op.cc
        yajlpp/json_ptr.cc
        line_buffer.cc
        listview_curses.cc
        lnav_commands.cc
        lnav_config.cc
        base/lnav_log.cc
        lnav_util.cc
        log_accel.cc
        log_actions.cc
        log_data_helper.cc
        log_data_table.cc
        log_format.cc
        log_format_loader.cc
        log_level.cc
        log_search_table.cc
        logfile.cc
        logfile_sub_source.cc
        network-extension-functions.cc
        data_scanner.cc
        data_scanner_re.cc
        data_parser.cc
        papertrail_proc.cc
        ptimec_rt.cc
        pretty_printer.cc
        pugixml/pugixml.cpp
        readline_callbacks.cc
        readline_curses.cc
        readline_highlighters.cc
        readline_possibilities.cc
        regexp_vtab.cc
        relative_time.cc
        session_data.cc
        sequence_matcher.cc
        shared_buffer.cc
        shlex.cc
        sqlite-extension-func.cc
        statusview_curses.cc
        string-extension-functions.cc
        sysclip.cc
        pcrepp/pcrepp.cc
        piper_proc.cc
        spectro_source.cc
        sql_util.cc
        state-extension-functions.cc
        styling.cc
        base/string_util.cc
        string_attr_type.cc
        strnatcmp.c
        text_format.cc
        textfile_highlighters.cc
        textfile_sub_source.cc
        textview_curses.cc
        top_status_source.cc
        base/time_util.cc
        time-extension-functions.cc
        timer.cc
        unique_path.cc
        unique_path.hh
        view_curses.cc
        view_helpers.cc
        views_vtab.cc
        vt52_curses.cc
        vtab_module.cc
        log_vtab_impl.cc
        xml_util.cc
        xpath_vtab.cc
        xterm_mouse.cc
        yajlpp/yajlpp.cc
        yajl/yajl.c
        yajl/yajl_alloc.c
        yajl/yajl_alloc.h
        yajl/yajl_buf.c
        yajl/yajl_buf.h
        yajl/yajl_bytestack.h
        yajl/yajl_encode.c
        yajl/yajl_encode.h
        yajl/yajl_gen.c
        yajl/yajl_lex.c
        yajl/yajl_lex.h
        yajl/yajl_parser.c
        yajl/yajl_parser.h
        yajl/yajl_tree.c
        yajl/yajl_version.c
        spookyhash/SpookyV2.cpp

        all_logs_vtab.hh
        archive_manager.hh
        archive_manager.cfg.hh
        attr_line.hh
        auto_fd.hh
        auto_mem.hh
        auto_pid.hh
        big_array.hh
        bottom_status_source.hh
        bound_tags.hh
        byte_array.hh
        command_executor.hh
        column_namer.hh
        curl_looper.hh
        base/date_time_scanner.hh
        doc_status_source.hh
        elem_to_json.hh
        base/enum_util.hh
        base/func_util.hh
        base/future_util.hh
        field_overlay_source.hh
        file_collection.hh
        file_format.hh
        files_sub_source.hh
        filter_observer.hh
        filter_status_source.hh
        filter_sub_source.hh
        fstat_vtab.hh
        fts_fuzzy_match.hh
        grep_highlighter.hh
        help_text.hh
        help_text_formatter.hh
        highlighter.hh
        hotkeys.hh
        base/humanize.hh
        input_dispatcher.hh
        base/injector.hh
        base/injector.bind.hh
        base/intern_string.hh
        base/is_utf8.hh
        base/isc.hh
        k_merge_tree.h
        log_actions.hh
        log_data_helper.hh
        log_data_table.hh
        log_format.hh
        log_format_ext.hh
        log_format_fwd.hh
        log_format_impls.cc
        log_gutter_source.hh
        log_level.hh
        log_search_table.hh
        logfile.hh
        logfile_fwd.hh
        logfile_stats.hh
        base/math_util.hh
        optional.hpp
        papertrail_proc.hh
        plain_text_source.hh
        pretty_printer.hh
        preview_status_source.hh
        ptimec.hh
        pugixml/pugiconfig.hpp
        pugixml/pugixml.hpp
        readline_callbacks.hh
        readline_possibilities.hh
        regexp_vtab.hh
        relative_time.hh
        base/result.h
        base/time_util.hh
        styling.hh
        ring_span.hh
        safe/accessmode.h
        safe/defaulttypes.h
        safe/mutableref.h
        safe/safe.h
        sequence_sink.hh
        shlex.hh
        simdutf8check.h
        spectro_source.hh
        strong_int.hh
        string_attr_type.hh
        sysclip.hh
        term_extra.hh
        termios_guard.hh
        text_format.hh
        textfile_highlighters.hh
        textfile_sub_source.hh
        textview_curses.hh
        textview_curses_fwd.hh
        time_T.hh
        timer.hh
        top_status_source.hh
        url_loader.hh
        view_helpers.hh
        views_vtab.hh
        vis_line.hh
        vtab_module.hh
        vtab_module_json.hh
        yajlpp/yajlpp.hh
        yajlpp/yajlpp_def.hh
        xml_util.hh
        xpath_vtab.hh

        mapbox/recursive_wrapper.hpp
        mapbox/variant.hpp
        mapbox/variant_io.hpp
        mapbox/variant_visitor.hpp

        yajl/api/yajl_common.h
        yajl/api/yajl_gen.h
        yajl/api/yajl_parse.h
        yajl/api/yajl_tree.h

        ghc/filesystem.hpp
        ghc/fs_fwd.hpp
        ghc/fs_impl.hpp
        ghc/fs_std.hpp
        ghc/fs_std_fwd.hpp
        ghc/fs_std_impl.hpp

        fmtlib/format.cc
        fmtlib/os.cc
        fmtlib/fmt/chrono.h
        fmtlib/fmt/color.h
        fmtlib/fmt/compile.h
        fmtlib/fmt/core.h
        fmtlib/fmt/format-inl.h
        fmtlib/fmt/format.h
        fmtlib/fmt/locale.h
        fmtlib/fmt/os.h
        fmtlib/fmt/ostream.h
        fmtlib/fmt/posix.h
        fmtlib/fmt/printf.h
        fmtlib/fmt/ranges.h
        fmtlib/fmt/time.h

        ww898/cp_utf8.hpp

        log_level_re.cc
        )

set(lnav_SRCS lnav.cc)

target_include_directories(
        diag
        PUBLIC
        .
        fmtlib
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(diag ${lnav_LIBS})

check_library_exists(util openpty "" HAVE_LIBUTIL)

if(HAVE_LIBUTIL)
    target_link_libraries(diag util)
endif()

add_executable(test_yajlpp yajlpp/test_yajlpp.cc)
target_link_libraries(test_yajlpp diag ${lnav_LIBS})
add_test(NAME test_yajlpp COMMAND test_yajlpp)

add_executable(drive_json_op yajlpp/drive_json_op.cc)
target_link_libraries(drive_json_op diag ${lnav_LIBS})

add_executable(lnav ${lnav_SRCS})
target_link_libraries(lnav diag)

install(TARGETS lnav DESTINATION bin)
