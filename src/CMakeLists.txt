include(CheckTypeSize)
include(CheckIncludeFile)
include(CheckLibraryExists)
include(CheckFunctionExists)

check_type_size(off_t SIZEOF_OFF_T)

check_include_file("pty.h" HAVE_PTY_H)
check_include_file("util.h" HAVE_UTIL_H)
check_include_file("execinfo.h" HAVE_EXECINFO_H)
check_include_file("libproc.h" HAVE_LIBPROC_H)

set(VCS_PACKAGE_STRING "lnav ${CMAKE_PROJECT_VERSION}")
set(PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")

configure_file(config.cmake.h.in config.h)

add_subdirectory(base)
add_subdirectory(pcrepp)
add_subdirectory(remote)
add_subdirectory(tailer)
add_subdirectory(formats/logfmt)
add_subdirectory(yajl)
add_subdirectory(yajlpp)

add_executable(bin2c bin2c.hh ../tools/bin2c.c)
target_link_libraries(bin2c ZLIB::ZLIB)

add_executable(ptimec ptimec.hh ptimec.cc)

set(TIME_FORMATS
        "@%@"
        "%Y-%m-%d %H:%M:%S.%f%z"
        "%Y-%m-%d %H:%M:%S,%f%z"
        "%Y-%m-%d %H:%M:%S.%f %Z"
        "%Y-%m-%d %H:%M:%S,%f %Z"
        "%Y-%m-%d %H:%M:%S,%L%z"
        "%Y-%m-%d %H:%M:%S,%L %z"
        "%Y-%m-%d %H:%M:%S.%L%z"
        "%Y-%m-%d %H:%M:%S.%L %z"
        "%Y-%m-%d %H:%M:%S.%L %Z"
        "%Y-%m-%d %H:%M:%S,%L"
        "%Y-%m-%d %H:%M:%S.%L"
        "%Y-%m-%d %H:%M:%S%z"
        "%Y-%m-%d %H:%M:%S %Z"
        "%Y-%m-%d %H:%M:%S %z"
        "%Y-%m-%d %H:%M:%S:%L"
        "%Y-%m-%d %H:%M:%S"
        "%Y-%m-%d %H:%M"
        "%Y-%jT%H:%M:%S %p %Z"
        "%Y-%m-%dT%H:%M:%S %p %Z"
        "%Y-%jT%H:%M:%S.%N%z"
        "%Y-%m-%dT%H:%M:%S.%N%z"
        "%y-%m-%dT%H:%M:%S.%N%z"
        "%Y-%jT%H:%M:%S.%f%z"
        "%Y-%m-%dT%H:%M:%S.%f%z"
        "%y-%m-%dT%H:%M:%S.%f%z"
        "%Y-%jT%H:%M:%S.%L%z"
        "%Y-%m-%dT%H:%M:%S.%L%z"
        "%y-%m-%dT%H:%M:%S.%L%z"
        "%Y-%jT%H:%M:%S.%L%Z"
        "%Y-%m-%dT%H:%M:%S.%L%Z"
        "%y-%m-%dT%H:%M:%S.%L%Z"
        "%Y-%m-%dT%H:%M:%S%z"
        "%Y-%m-%dT%H:%M:%S%z"
        "%Y-%m-%dT%H:%M:%S"
        "%Y-%m-%dT%H:%M"
        "%Y/%m/%d %H:%M:%S %z"
        "%Y/%m/%d %H:%M:%S%z"
        "%Y/%m/%d %H:%M:%S.%f%z"
        "%Y/%m/%d %H:%M:%S.%f%Z"
        "%Y/%m/%d %H:%M:%S.%f %z"
        "%Y/%m/%d %H:%M:%S.%f %Z"
        "%Y/%m/%d %H:%M:%S.%f"
        "%Y/%m/%d %H:%M:%S.%L"
        "%Y/%m/%d %H:%M:%S"
        "%Y/%m/%d %H:%M"
        "%Y %b %d %a %H:%M:%S.%L"
        "%Y %b %d %H:%M:%S.%L"
        "%Y %b %d %H:%M:%S"
        "%a %b %d %H:%M:%S %Y"
        "%a %b %d %H:%M:%S.%f %Y"
        "%a %b %d %H:%M:%S:%f %Y"
        "%a %b %d %H:%M:%S %Z %Y"
        "%a %b %d %I:%M:%S %p %Z %Y"
        "%a %b %d %H:%M:%S "
        "%a %b %d %H:%M:%S.%L "
        "%a %b %d %H:%M "
        "%a %b %e %H:%M:%S %Z %Y"
        "%d/%m/%y %H:%M:%S"
        "%d/%b/%Y:%H:%M:%S %z"
        "%d-%b-%Y %H:%M:%S %z"
        "%d-%b-%Y %H:%M:%S %Z"
        "%d %b %Y %H:%M:%S"
        "%d %b %Y %H:%M:%S.%L%z"
        "%d %b %Y %H:%M:%S,%L%z"
        "%d %b %Y %H:%M:%S.%L"
        "%d %b %Y %H:%M:%S,%L"
        "%d %b %Y %H:%M"
        "%b-%d %H:%M:%S"
        "%b %d %H:%M:%S"
        "%b %d %k:%M:%S"
        "%b %d %l:%M:%S"
        "%b %d %l:%M"
        "%b %e, %Y %l:%M:%S %p"
        "%b %d, %Y %l:%M:%S %p"
        "%m/%d/%Y %I:%M:%S:%L %p %Z"
        "%m/%d/%Y %I:%M:%S %p %Z"
        "%m/%d/%Y %l:%M:%S %p %Z"
        "%m/%d/%Y %I:%M:%S %p%z"
        "%m/%d/%Y %l:%M:%S %p%z"
        "%m/%e/%Y %l:%M:%S %p%z"
        "%m/%e/%Y %l:%M:%S %p %z"
        "%m/%e/%Y %I:%M:%S %p"
        "%m/%e/%Y %l:%M:%S%p"
        "%m/%d/%y %H:%M:%S"
        "%m/%d/%Y %H:%M:%S"
        "%d/%b/%Y %H:%M:%S"
        "%d/%b/%y %H:%M:%S"
        "%m.%d %H:%M:%S"
        "%m%d %H:%M:%S"
        "%y%m%d %H%M%S"
        "%Y%m%d %H:%M:%S"
        "%Y%m%d.%H%M%S"
        "%H:%M:%S"
        "%H:%M:%S.%f"
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
        DEPENDS ptimec
        COMMAND ptimec ${TIME_FORMATS} > time_fmts.cc)

add_library(lnavdt STATIC config.h.in ptimec.hh ptimec_rt.cc time_fmts.cc)
target_include_directories(lnavdt PUBLIC . ${CMAKE_CURRENT_BINARY_DIR} third-party/date/include)

function(bin2c)
    cmake_parse_arguments(BIN2C_ "" "VARNAME" "" ${ARGN})

    list(TRANSFORM BIN2C_UNPARSED_ARGUMENTS "\\." "-")
    add_custom_command(
            OUTPUT "${DST_FILE}.h" "${DST_FILE}.cc"
            COMMAND bin2c "${DST_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/${FILE_TO_LINK}"
            DEPENDS bin2c "${FILE_TO_LINK}")
endfunction(bin2c)

foreach (FILE_TO_LINK animals.json ansi-palette.json css-color-names.json diseases.json emojis.json xml-entities.json xterm-palette.json help.txt help.md init.sql words.json)
    string(REPLACE "." "-" DST_FILE "${FILE_TO_LINK}")
    add_custom_command(
            OUTPUT "${DST_FILE}.h" "${DST_FILE}.cc"
            COMMAND bin2c "${DST_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/${FILE_TO_LINK}"
            DEPENDS bin2c "${FILE_TO_LINK}")
    list(APPEND GEN_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${DST_FILE}.h"
            "${CMAKE_CURRENT_BINARY_DIR}/${DST_FILE}.cc")
endforeach (FILE_TO_LINK)

set(FORMAT_FILES
        formats/access_log.json
        formats/alb_log.json
        formats/block_log.json
        formats/bunyan_log.json
        formats/candlepin_log.json
        formats/choose_repo_log.json
        formats/cloudflare_log.json
        formats/cloudvm_ram_log.json
        formats/cups_log.json
        formats/dpkg_log.json
        formats/ecs_log.json
        formats/elb_log.json
        formats/engine_log.json
        formats/error_log.json
        formats/esx_syslog_log.json
        formats/fsck_hfs_log.json
        formats/github_events_log.json
        formats/glog_log.json
        formats/haproxy_log.json
        formats/java_log.json
        formats/journald_json_log.json
        formats/katello_log.json
        formats/lnav_debug_log.json
        formats/nextcloud_log.json
        formats/nextflow_log.json
        formats/openam_log.json
        formats/openamdb_log.json
        formats/openstack_log.json
        formats/page_log.json
        formats/pcap_log.json
        formats/procstate_log.json
        formats/proxifier_log.json
        formats/redis_log.json
        formats/snaplogic_log.json
        formats/sssd_log.json
        formats/strace_log.json
        formats/sudo_log.json
        formats/syslog_log.json
        formats/s3_log.json
        formats/tcf_log.json
        formats/tcsh_history.json
        formats/unifi_log.json
        formats/uwsgi_log.json
        formats/vdsm_log.json
        formats/vmk_log.json
        formats/vmw_log.json
        formats/vmw_vc_svc_log.json
        formats/vmw_py_log.json
        formats/vpostgres_log.json
        formats/xmlrpc_log.json
        formats/zookeeper_log.json
)

set(FORMAT_FILE_PATHS ${FORMAT_FILES})

list(TRANSFORM FORMAT_FILE_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_custom_command(
        OUTPUT default-formats.h default-formats.cc
        COMMAND bin2c -n lnav_format_json default-formats ${FORMAT_FILE_PATHS}
        DEPENDS bin2c ${FORMAT_FILES})
list(APPEND GEN_SRCS default-formats.h default-formats.cc)

set(PRQL_FILES
        prql/stats.prql
        prql/utils.prql)

set(PRQL_FILE_PATHS ${PRQL_FILES})

list(TRANSFORM PRQL_FILE_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_custom_command(
        OUTPUT prql-modules.h prql-modules.cc
        COMMAND bin2c -n lnav_prql_modules prql-modules ${PRQL_FILE_PATHS}
        DEPENDS bin2c ${PRQL_FILES})
list(APPEND GEN_SRCS prql-modules.h prql-modules.cc)

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
        themes/solarized-light.json)

set(CONFIG_FILE_PATHS ${CONFIG_FILES})

list(TRANSFORM CONFIG_FILE_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_custom_command(
        OUTPUT default-config.h default-config.cc
        COMMAND bin2c -n lnav_config_json default-config ${CONFIG_FILE_PATHS}
        DEPENDS bin2c ${CONFIG_FILES})
list(APPEND GEN_SRCS default-config.h default-config.cc)

set(BUILTIN_LNAV_SCRIPTS
        scripts/dhclient-summary.lnav
        scripts/docker-url-handler.lnav
        scripts/find-msg.lnav
        scripts/find-chained-msg.lnav
        scripts/journald-url-handler.lnav
        scripts/lnav-copy-text.lnav
        scripts/lnav-link-callback.lnav
        scripts/lnav-pop-view.lnav
        scripts/partition-by-boot.lnav
        scripts/piper-url-handler.lnav
        scripts/rename-stdin.lnav
        scripts/report-access-log.lnav
        scripts/search-for.lnav
        scripts/zk-set-ops.lnav
)

set(BUILTIN_LNAV_SCRIPT_PATHS ${BUILTIN_LNAV_SCRIPTS})

list(TRANSFORM BUILTIN_LNAV_SCRIPT_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_custom_command(
        OUTPUT builtin-scripts.h builtin-scripts.cc
        COMMAND bin2c -n lnav_scripts builtin-scripts ${BUILTIN_LNAV_SCRIPT_PATHS}
        DEPENDS bin2c ${BUILTIN_LNAV_SCRIPTS})
list(APPEND GEN_SRCS builtin-scripts.h builtin-scripts.cc)

set(BUILTIN_SH_SCRIPTS
        scripts/com.vmware.btresolver.py
        scripts/dump-pid.sh
        scripts/pcap_log-converter.sh
        scripts/zookeeper.sql
)

set(BUILTIN_SH_SCRIPT_PATHS ${BUILTIN_SH_SCRIPTS})

list(TRANSFORM BUILTIN_SH_SCRIPT_PATHS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_custom_command(
        OUTPUT builtin-sh-scripts.h builtin-sh-scripts.cc
        COMMAND bin2c -n lnav_sh_scripts builtin-sh-scripts ${BUILTIN_SH_SCRIPT_PATHS}
        DEPENDS bin2c ${BUILTIN_SH_SCRIPTS})
list(APPEND GEN_SRCS builtin-sh-scripts.h builtin-sh-scripts.cc)

add_library(
        cppfmt STATIC
        fmtlib/format.cc
        fmtlib/os.cc
        fmtlib/fmt/args.h
        fmtlib/fmt/chrono.h
        fmtlib/fmt/color.h
        fmtlib/fmt/compile.h
        fmtlib/fmt/core.h
        fmtlib/fmt/format-inl.h
        fmtlib/fmt/format.h
        fmtlib/fmt/locale.h
        fmtlib/fmt/os.h
        fmtlib/fmt/ostream.h
        fmtlib/fmt/printf.h
        fmtlib/fmt/ranges.h
        fmtlib/fmt/std.h
        fmtlib/fmt/xchar.h
)
target_include_directories(cppfmt PUBLIC fmtlib)

add_library(
        datepp STATIC
        third-party/date/src/tz.cpp
)
target_include_directories(datepp PUBLIC third-party/date/include)
target_link_libraries(datepp ${lnav_LIBS})

add_library(
        cppscnlib STATIC
        third-party/scnlib/src/scn/impl.cpp
        third-party/scnlib/src/scn/impl.h

        third-party/scnlib/include/scn/fwd.h
        third-party/scnlib/include/scn/ranges.h
        third-party/scnlib/include/scn/xchar.h
        third-party/scnlib/include/scn/chrono.h
        third-party/scnlib/include/scn/regex.h
        third-party/scnlib/include/scn/istream.h
        third-party/scnlib/include/scn/scan.h
)
target_include_directories(
        cppscnlib PRIVATE
        third-party/scnlib/src
        third-party/scnlib/src/deps/fast_float/single_include)
target_include_directories(cppscnlib PUBLIC third-party/scnlib/include)

add_library(
        base64 STATIC
        third-party/base64/lib/lib.c
        third-party/base64/lib/arch/generic/codec.c
        third-party/base64/lib/tables/tables.c
)
target_include_directories(base64 PRIVATE third-party/base64/lib)
target_include_directories(base64 PUBLIC third-party/base64/include)

add_library(
        lnavnotcurses STATIC
        third-party/notcurses/src/compat/compat.c
        third-party/notcurses/src/lib/automaton.c
        third-party/notcurses/src/lib/blit.c
        third-party/notcurses/src/lib/debug.c
        third-party/notcurses/src/lib/direct.c
        third-party/notcurses/src/lib/fade.c
        third-party/notcurses/src/lib/fd.c
        third-party/notcurses/src/lib/fill.c
        third-party/notcurses/src/lib/gpm.c
        third-party/notcurses/src/lib/in.c
        third-party/notcurses/src/lib/kitty.c
        third-party/notcurses/src/lib/layout.c
        third-party/notcurses/src/lib/linux.c
        third-party/notcurses/src/lib/menu.c
        third-party/notcurses/src/lib/metric.c
        third-party/notcurses/src/lib/mice.c
        third-party/notcurses/src/lib/notcurses.c
        third-party/notcurses/src/lib/plot.c
        third-party/notcurses/src/lib/progbar.c
        third-party/notcurses/src/lib/reader.c
        third-party/notcurses/src/lib/reel.c
        third-party/notcurses/src/lib/render.c
        third-party/notcurses/src/lib/selector.c
        third-party/notcurses/src/lib/sixel.c
        third-party/notcurses/src/lib/sprite.c
        third-party/notcurses/src/lib/stats.c
        third-party/notcurses/src/lib/tabbed.c
        third-party/notcurses/src/lib/termdesc.c
        third-party/notcurses/src/lib/tree.c
        third-party/notcurses/src/lib/unixsig.c
        third-party/notcurses/src/lib/util.c
        third-party/notcurses/src/lib/visual.c
        third-party/notcurses/src/lib/windows.c
)
target_include_directories(lnavnotcurses PRIVATE
        third-party/notcurses/src/lib
        third-party/notcurses/src/
        ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
)
target_include_directories(lnavnotcurses PUBLIC third-party/notcurses/include)

add_library(
        spookyhash STATIC
        spookyhash/SpookyV2.cpp
)

add_library(lnavfileio STATIC
        grep_proc.hh
        line_buffer.hh
        log_level.hh
        piper.header.hh
        pollable.hh
        shared_buffer.hh

        grep_proc.cc
        line_buffer.cc
        log_level.cc
        piper.header.cc
        pollable.cc
        shared_buffer.cc
)
target_include_directories(lnavfileio PRIVATE . ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(lnavfileio cppfmt spookyhash pcrepp base BZip2::BZip2 ZLIB::ZLIB yajlpp)

add_library(
        diag STATIC
        ${GEN_SRCS}
        config.h.in
        all_logs_vtab.cc
        archive_manager.cc
        document.sections.cc
        bin2c.hh
        bin2c_rt.cc
        bookmarks.cc
        bottom_status_source.cc
        breadcrumb_curses.cc
        collation-functions.cc
        column_namer.cc
        command_executor.cc
        crashd.client.cc
        curl_looper.cc
        db_sub_source.cc
        dump_internals.cc
        elem_to_json.cc
        environ_vtab.cc
        extension-functions.cc
        external_opener.cc
        field_overlay_source.cc
        file_collection.cc
        file_converter_manager.cc
        file_format.cc
        file_options.cc
        file_vtab.cc
        files_sub_source.cc
        filter_observer.cc
        filter_status_source.cc
        filter_sub_source.cc
        fs-extension-functions.cc
        fstat_vtab.cc
        fts_fuzzy_match.cc
        help_text.cc
        help_text_formatter.cc
        highlighter.cc
        hist_source.cc
        hotkeys.cc
        input_dispatcher.cc
        json-extension-functions.cc
        listview_curses.cc
        lnav.events.cc
        lnav.indexing.cc
        lnav.management_cli.cc
        lnav_commands.cc
        lnav_config.cc
        lnav_util.cc
        log.annotate.cc
        log.watch.cc
        log_accel.cc
        log_actions.cc
        log_data_helper.cc
        log_data_table.cc
        log_format.cc
        log_format_loader.cc
        log_search_table.cc
        logfile.cc
        logfile_sub_source.cc
        md2attr_line.cc
        md4cpp.cc
        network-extension-functions.cc
        data_scanner.cc
        data_scanner_re.cc
        data_parser.cc
        file_converter_manager.cc
        piper.looper.cc
        piper.match.cc
        plain_text_source.cc
        pretty_printer.cc
        pugixml/pugixml.cpp
        readline_callbacks.cc
        readline_curses.cc
        readline_highlighters.cc
        readline_possibilities.cc
        regexp_vtab.cc
        regex101.client.cc
        regex101.import.cc
        relative_time.cc
        session.export.cc
        session_data.cc
        sequence_matcher.cc
        shlex.cc
        sqlite-extension-func.cc
        static_file_vtab.cc
        statusview_curses.cc
        string-extension-functions.cc
        sysclip.cc
        spectro_impls.cc
        spectro_source.cc
        sql_commands.cc
        sql_util.cc
        sqlitepp.cc
        state-extension-functions.cc
        styling.cc
        text_anonymizer.cc
        text_format.cc
        text_overlay_menu.cc
        textfile_highlighters.cc
        textfile_sub_source.cc
        textview_curses.cc
        timeline_source.cc
        top_status_source.cc
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
        yaml-extension-functions.cc
        third-party/md4c/md4c.c
        third-party/sqlite/ext/series.c
        third-party/sqlite/ext/dbdump.c

        all_logs_vtab.hh
        archive_manager.hh
        archive_manager.cfg.hh
        document.sections.hh
        big_array.hh
        bookmarks.hh
        bookmarks.json.hh
        bottom_status_source.hh
        bound_tags.hh
        breadcrumb.hh
        breadcrumb_curses.hh
        byte_array.hh
        command_executor.hh
        column_namer.hh
        crashd.client.hh
        curl_looper.hh
        doc_status_source.hh
        dump_internals.hh
        elem_to_json.hh
        external_opener.hh
        external_opener.cfg.hh
        field_overlay_source.hh
        file_collection.hh
        file_converter_manager.hh
        file_format.hh
        file_options.hh
        files_sub_source.hh
        filter_observer.hh
        filter_status_source.hh
        filter_sub_source.hh
        fstat_vtab.hh
        fts_fuzzy_match.hh
        grep_highlighter.hh
        hasher.hh
        help_text.hh
        help_text_formatter.hh
        highlighter.hh
        hotkeys.hh
        input_dispatcher.hh
        itertools.similar.hh
        k_merge_tree.h
        lnav.events.hh
        lnav.indexing.hh
        lnav.management_cli.hh
        lnav_config.hh
        lnav_config_fwd.hh
        lnav_util.hh
        log.annotate.hh
        log.annotate.cfg.hh
        log.watch.hh
        log_actions.hh
        log_data_helper.hh
        log_data_table.hh
        log_format.hh
        log_format_ext.hh
        log_format_fwd.hh
        log_format_impls.cc
        log_gutter_source.hh
        log_search_table.hh
        log_search_table_fwd.hh
        logfile_sub_source.cfg.hh
        logfile.hh
        logfile_fwd.hh
        logfile_stats.hh
        md2attr_line.hh
        md4cpp.hh
        file_converter_manager.hh
        piper.looper.cfg.hh
        piper.looper.hh
        piper.match.hh
        plain_text_source.hh
        pretty_printer.hh
        preview_status_source.hh
        pugixml/pugiconfig.hpp
        pugixml/pugixml.hpp
        readline_callbacks.hh
        readline_context.hh
        readline_possibilities.hh
        regex101.client.hh
        regex101.import.hh
        regexp_vtab.hh
        relative_time.hh
        styling.hh
        ring_span.hh
        safe/accessmode.h
        safe/defaulttypes.h
        safe/mutableref.h
        safe/safe.h
        session.export.hh
        sequence_sink.hh
        shlex.hh
        shlex.resolver.hh
        simdutf8check.h
        spectro_impls.hh
        spectro_source.hh
        sqlitepp.hh
        sql_execute.hh
        sql_help.hh
        sql_util.hh
        static_file_vtab.hh
        strong_int.hh
        sysclip.hh
        sysclip.cfg.hh
        term_extra.hh
        termios_guard.hh
        text_anonymizer.hh
        text_format.hh
        text_overlay_menu.hh
        textfile_highlighters.hh
        textfile_sub_source.hh
        textfile_sub_source.cfg.hh
        textview_curses.hh
        textview_curses_fwd.hh
        time_T.hh
        timeline_source.hh
        timeline_status_source.hh
        timer.hh
        top_status_source.hh
        url_handler.cfg.hh
        url_loader.hh
        view_helpers.hh
        view_helpers.crumbs.hh
        view_helpers.examples.hh
        view_helpers.hist.hh
        views_vtab.hh
        vis_line.hh
        vtab_module.hh
        vtab_module_json.hh
        xml_util.hh
        xpath_vtab.hh
        mapbox/recursive_wrapper.hpp
        mapbox/variant.hpp
        mapbox/variant_io.hpp
        mapbox/variant_visitor.hpp
        ww898/cp_utf8.hpp
        log_level_re.cc

        third-party/ArenaAlloc/arenaalloc.h
        third-party/ArenaAlloc/arenaallocimpl.h

        third-party/CLI/StringTools.hpp
        third-party/CLI/App.hpp
        third-party/CLI/Macros.hpp
        third-party/CLI/Option.hpp
        third-party/CLI/Config.hpp
        third-party/CLI/CLI.hpp
        third-party/CLI/Formatter.hpp
        third-party/CLI/Error.hpp
        third-party/CLI/Version.hpp
        third-party/CLI/Timer.hpp
        third-party/CLI/FormatterFwd.hpp
        third-party/CLI/Validators.hpp
        third-party/CLI/Split.hpp
        third-party/CLI/TypeTools.hpp
        third-party/CLI/ConfigFwd.hpp

        third-party/date/include/date/tz_private.h
        third-party/date/include/date/ios.h
        third-party/date/include/date/ptz.h
        third-party/date/include/date/solar_hijri.h
        third-party/date/include/date/date.h
        third-party/date/include/date/julian.h
        third-party/date/include/date/chrono_io.h
        third-party/date/include/date/iso_week.h
        third-party/date/include/date/tz.h
        third-party/date/include/date/islamic.h

        third-party/intervaltree/IntervalTree.h

        third-party/md4c/md4c.h

        third-party/robin_hood/robin_hood.h

        third-party/prqlc-c/prqlc.cxx.hh
        third-party/prqlc-c/prqlc.cxx.cc
)

set(lnav_SRCS lnav.cc)

target_include_directories(diag PUBLIC . fmtlib ${CMAKE_CURRENT_BINARY_DIR}
        third-party
        third-party/base64/include
        third-party/date/include
        third-party/prqlc-c
        third-party/rapidyaml
        ${CURSES_INCLUDE_DIRS}
)
target_compile_options(diag PRIVATE ${CURSES_CFLAGS})

target_link_libraries(
        diag
        base
        datepp
        lnavdt
        lnavfileio
        lnavnotcurses
        pcrepp
        tailerservice
        tailerpp
        tailercommon
        logfmt
        yajlpp
        cppfmt
        base64
        spookyhash
        ${lnav_LIBS}
)
target_compile_definitions(diag PRIVATE SQLITE_OMIT_LOAD_EXTENSION)

check_library_exists(util openpty "" HAVE_LIBUTIL)

if (HAVE_LIBUTIL)
    target_link_libraries(diag util)
endif ()

add_executable(lnav ${lnav_SRCS})
target_link_libraries(lnav diag)

install(TARGETS lnav DESTINATION bin)
