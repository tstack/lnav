
include_directories(../../lbuild/src ../src/ /opt/local/include)

add_executable(test_chunky_index test_chunky_index.cc)
add_executable(test_pcrepp test_pcrepp.cc ../src/lnav_log.cc ../src/pcrepp.cc)
link_directories(/opt/local/lib)
target_link_libraries(test_pcrepp /opt/local/lib/libpcre.a)
