set(SOURCES device_data_proxy.cc update_events.cc)

set(HEADERS device_data_proxy.h update_events.h)

set(TARGET torizon_primary_extra)

add_library(${TARGET} STATIC
  ${SOURCES}
)

target_include_directories(${TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

aktualizr_source_file_checks(${HEADERS} ${SOURCES})
