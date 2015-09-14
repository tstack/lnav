
include_directories(../../lbuild/src ../src/ /opt/local/include)

add_executable(test_chunky_index test_chunky_index.cc)
add_executable(test_pcrepp test_pcrepp.cc ../src/lnav_log.cc ../src/pcrepp.cc)
add_executable(test_reltime test_reltime.cc
        ../src/relative_time.cc
        ../src/pcrepp.cc
        ../src/lnav_log.cc)
link_directories(/opt/local/lib)
target_link_libraries(test_pcrepp /opt/local/lib/libpcre.a)
target_link_libraries(test_reltime /opt/local/lib/libpcre.a)
