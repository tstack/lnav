find_package(PkgConfig REQUIRED)

pkg_search_module(pcre2 REQUIRED libpcre2-8 libpcre2-16 libpcre2-32 libpcre2-posix IMPORTED_TARGET GLOBAL)
add_library(pcre2::pcre2 ALIAS PkgConfig::pcre2)
