# - Find Miniupnp
# This module defines
# MINIUPNP_INCLUDE_DIR, where to find Miniupnp headers
# MINIUPNP_LIBRARY, Miniupnp libraries
# MINIUPNP_FOUND, If false, do not try to use Miniupnp

set(MINIUPNP_PREFIX "" CACHE PATH "path ")

find_path(MINIUPNP_INCLUDE_DIR miniupnpc.h
        PATHS ${MINIUPNP_PREFIX}/include /usr/include /usr/local/include
        PATH_SUFFIXES miniupnpc)

find_library(MINIUPNP_LIBRARY NAMES miniupnpc libminiupnpc
        PATHS ${MINIUPNP_PREFIX}/lib /usr/lib /usr/local/lib)

if(MINIUPNP_INCLUDE_DIR AND MINIUPNP_LIBRARY)
    get_filename_component(MINIUPNP_LIBRARY_DIR ${MINIUPNP_LIBRARY} PATH)
    set(MINIUPNP_FOUND TRUE)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
        Miniupnpc DEFAULT_MSG
        MINIUPNP_INCLUDE_DIR
        MINIUPNP_LIBRARY
)

if(MINIUPNP_FOUND)
    file(STRINGS "${MINIUPNP_INCLUDE_DIR}/miniupnpc.h" MINIUPNPC_API_VERSION_STR REGEX "^#define[\t ]+MINIUPNPC_API_VERSION[\t ]+[0-9]+")
    if(MINIUPNPC_API_VERSION_STR MATCHES "^#define[\t ]+MINIUPNPC_API_VERSION[\t ]+([0-9]+)")
        set(MINIUPNPC_API_VERSION "${CMAKE_MATCH_1}")
        if (${MINIUPNPC_API_VERSION} GREATER "10" OR ${MINIUPNPC_API_VERSION} EQUAL "10")
            if(NOT Miniupnp_FIND_QUIETLY)
                message(STATUS "Found Miniupnpc API version " ${MINIUPNPC_API_VERSION})
            endif()
            set(MINIUPNP_FOUND true)
        else()
            message(FATAL_ERROR "Unsupported Miniupnpc version!")
        endif()
    endif()
else()
    if(MINIUPNP_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find Miniupnp")
    endif()
endif()

mark_as_advanced(
        MINIUPNP_LIBRARY
        MINIUPNP_INCLUDE_DIR
)