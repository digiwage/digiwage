# - Find NAT-PMP
# This module defines
# NAT-PMP_INCLUDE_DIR, where to find NAT-PMP headers
# NAT-PMP_LIBRARY, NAT-PMP libraries
# NAT-PMP_FOUND, If false, do not try to use NAT-PMP

set(NAT-PMP_PREFIX "" CACHE PATH "path ")

find_path(NAT-PMP_INCLUDE_DIR natpmp.h
        PATHS ${NAT-PMP_PREFIX}/include /usr/include /usr/local/include)

find_library(NAT-PMP_LIBRARY NAMES natpmp libnatpmp
        PATHS ${NAT-PMP_PREFIX}/lib /usr/lib /usr/local/lib)

if(NAT-PMP_INCLUDE_DIR AND NAT-PMP_LIBRARY)
    get_filename_component(NAT-PMP_LIBRARY_DIR ${NAT-PMP_LIBRARY} PATH)
    set(NAT-PMP_FOUND TRUE)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
        NAT-PMP DEFAULT_MSG
        NAT-PMP_INCLUDE_DIR
        NAT-PMP_LIBRARY
)

if(NAT-PMP_FOUND)
    if(NOT NAT-PMP_FIND_QUIETLY)
        MESSAGE(STATUS "Found NAT-PMP: ${NAT-PMP_LIBRARY}")
    endif()
else()
    if(NAT-PMP_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find NAT-PMP")
    endif()
endif()

mark_as_advanced(
        NAT-PMP_LIBRARY
        NAT-PMP_INCLUDE_DIR
)