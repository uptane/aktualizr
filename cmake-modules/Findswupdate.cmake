# Try to find Swupdate
# SWUPDATE_DIR - Hint path to a local build of Swupdate
# Once done this will define
#  SWUPDATE_FOUND
#  SWUPDATE_LIBRARIES
#

find_path(LIBSWUPDATE_INCLUDE_DIR
    NAMES network_ipc.h
    HINTS ${SWUPDATE_DIR}/include)

find_library(LIBSWUPDATE_LIBRARY
    NAMES libswupdate.so.0.1
    HINTS ${SWUPDATE_DIR}/lib)

message("libswupdate headers path: ${LIBSWUPDATE_INCLUDE_DIR}")
message("libswupdate library path: ${LIBSWUPDATE_LIBRARY}")

set(LIBSWUPDATE_INCLUDE_DIRS ${LIBOSTREE_INCLUDE_DIR})
set(LIBSWUPDATE_LIBRARIES ${LIBSWUPDATE_LIBRARY})
