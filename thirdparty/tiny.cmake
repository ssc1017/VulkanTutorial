set(TINY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tiny_obj)
add_library(tiny INTERFACE)
target_include_directories(tiny INTERFACE ${TINY_DIR})