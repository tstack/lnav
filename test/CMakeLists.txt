
include_directories(../../lbuild-debug/src ../src/ /opt/local/include)

add_executable(lnav_doctests
        lnav_doctests.cc
        ../src/relative_time.cc
        ../../lbuild-debug/src/time_fmts.cc
        ../src/ptimec_rt.cc
        ../src/pcrepp/pcrepp.cc
        ../src/base/lnav_log.cc)
add_executable(test_pcrepp test_pcrepp.cc ../src/base/lnav_log.cc ../src/pcrepp/pcrepp.cc)
add_executable(test_reltime test_reltime.cc
        ../src/relative_time.cc
        ../src/pcrepp/pcrepp.cc
        ../src/base/lnav_log.cc)
add_executable(test_date_time_scanner test_date_time_scanner.cc
        ../src/lnav_util.cc
        ../../lbuild-debug/src/time_fmts.cc
        ../src/ptimec_rt.cc
        ../src/pcrepp/pcrepp.cc
        ../src/base/lnav_log.cc
        ../src/spookyhash/SpookyV2.cpp)
add_executable(test_abbrev test_abbrev.cc
        ../src/lnav_util.cc
        ../../lbuild-debug/src/time_fmts.cc
        ../src/ptimec_rt.cc
        ../src/pcrepp/pcrepp.cc
        ../src/base/lnav_log.cc
        ../src/spookyhash/SpookyV2.cpp)
add_executable(drive_sql_anno drive_sql_anno.cc ../src/base/lnav_log.cc ../src/pcrepp/pcrepp.cc)
link_directories(/opt/local/lib)
target_link_libraries(test_pcrepp /usr/local/lib/libpcre.a)
target_link_libraries(test_reltime /usr/local/lib/libpcre.a)
target_link_libraries(lnav_doctests /usr/local/lib/libpcre.a)
target_link_libraries(test_date_time_scanner /usr/local/lib/libpcre.a)
target_link_libraries(test_abbrev /usr/local/lib/libpcre.a)
target_link_libraries(drive_sql_anno /usr/local/lib/libpcre.a)
