cmake_minimum_required(VERSION 3.10)

project(dkcomp)

set(DKCOMP_SRC 
  src/bigdata_comp.c
  src/bigdata_decomp.c
  src/dkcchr.c
  src/dkcgbc.c
  src/dk_comp_lib.c
  src/dk_error.c
  src/dkl_tilemap.c
  src/dkl_tileset.c
  src/gba_auto.c
  src/gbahuff20.c
  src/gbahuff50.c
  src/gbahuff60.c
  src/gba_lz77.c
  src/gba_rle.c
  src/gb_printer.c
  src/smalldata.c
)

add_library(dkcomp SHARED ${DKCOMP_SRC})
target_include_directories(dkcomp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(comp src/comp_util.c)
target_link_libraries(comp PRIVATE dkcomp)

add_executable(decomp src/decomp_util.c)
target_link_libraries(decomp PRIVATE dkcomp)

find_package(PkgConfig)

if(PkgConfig_FOUND)
  pkg_check_modules(MHD libmicrohttpd)
endif()

if(MHD_FOUND)
  add_executable(server src/server.c)
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server.html
    ${CMAKE_CURRENT_BINARY_DIR}/server.html
    COPYONLY
  )
  target_link_libraries(server PRIVATE dkcomp microhttpd)
endif()
