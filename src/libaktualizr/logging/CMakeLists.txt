set(SOURCES logging.cc logging_config.cc default_log_sink.cc)
set(HEADERS logging.h default_log_sink.h)

add_library(logging OBJECT ${SOURCES})
aktualizr_source_file_checks(${SOURCES} ${HEADERS})
