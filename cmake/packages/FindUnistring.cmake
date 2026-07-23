include(FindPackageHandleStandardArgs)

find_library(UNISTRING_LIBRARY NAMES unistring)
find_path(UNISTRING_HDR NAMES unistr.h)

find_package_handle_standard_args(Unistring
REQUIRED_VARS UNISTRING_LIBRARY UNISTRING_HDR)

if (Unistring_FOUND)
    mark_as_advanced(UNISTRING_LIBRARY)
    mark_as_advanced(UNISTRING_HDR)
    add_library(Unistring::Unistring UNKNOWN IMPORTED)
    set_property(TARGET Unistring::Unistring PROPERTY IMPORTED_LOCATION ${UNISTRING_LIBRARY})
    cmake_path(GET UNISTRING_HDR PARENT_PATH UNISTRING_HDR)
    target_include_directories(Unistring::Unistring INTERFACE ${UNISTRING_HDR_DIR})
endif ()
