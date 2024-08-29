find_path(LIBSWUPDATE_INCLUDE_DIR
    NAMES network_ipc.h
    HINTS ${SWUPDATE_DIR}/include)

find_library(LIBSWUPDATE_LIBRARY
    NAMES libswupdate.so.0.1
    HINTS ${SWUPDATE_DIR}/lib)

message("libswupdate headers path: ${LIBSWUPDATE_INCLUDE_DIR}")
message("libswupdate library path: ${LIBSWUPDATE_LIBRARY}")
