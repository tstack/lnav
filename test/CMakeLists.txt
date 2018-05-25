
include_directories(../../lbuild-debug/src ../src/ /opt/local/include)

add_executable(lnav_doctests
        lnav_doctests.cc
        ../src/relative_time.cc
        ../../lbuild-debug/src/time_fmts.cc
        ../src/ptimec_rt.cc
        ../src/pcrepp.cc
        ../src/lnav_log.cc)
add_executable(test_pcrepp test_pcrepp.cc ../src/lnav_log.cc ../src/pcrepp.cc)
add_executable(test_reltime test_reltime.cc
        ../src/relative_time.cc
        ../src/pcrepp.cc
        ../src/lnav_log.cc)
add_executable(test_date_time_scanner test_date_time_scanner.cc
        ../src/lnav_util.cc
        ../../lbuild-debug/src/time_fmts.cc
        ../src/ptimec_rt.cc
        ../src/pcrepp.cc
        ../src/lnav_log.cc
        ../src/spookyhash/SpookyV2.cpp)
add_executable(test_abbrev test_abbrev.cc
        ../src/lnav_util.cc
        ../../lbuild-debug/src/time_fmts.cc
        ../src/ptimec_rt.cc
        ../src/pcrepp.cc
        ../src/lnav_log.cc
        ../src/spookyhash/SpookyV2.cpp)
add_executable(drive_sql_anno drive_sql_anno.cc ../src/lnav_log.cc ../src/pcrepp.cc)
link_directories(/opt/local/lib)
target_link_libraries(test_pcrepp /opt/local/lib/libpcre.a)
target_link_libraries(test_reltime /opt/local/lib/libpcre.a)
target_link_libraries(lnav_doctests /opt/local/lib/libpcre.a)
target_link_libraries(test_date_time_scanner /opt/local/lib/libpcre.a)
target_link_libraries(test_abbrev /opt/local/lib/libpcre.a)
target_link_libraries(drive_sql_anno /opt/local/lib/libpcre.a)
