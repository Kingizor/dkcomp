cmake_minimum_required(VERSION 3.10)

project(dkcomp)

set(DKCOMP_SRC 
  bigdata_comp.c
  bigdata_decomp.c
  dkcchr.c
  dkcgbc.c
  dk_comp_lib.c
  dk_error.c
  dkl_tilemap.c
  dkl_tileset.c
  gba_auto.c
  gbahuff20.c
  gbahuff50.c
  gbahuff60.c
  gba_lz77.c
  gba_rle.c
  gb_printer.c
  smalldata.c
)

add_library(dkcomp SHARED ${DKCOMP_SRC})

add_executable(comp comp_util.c)
target_link_libraries(comp PRIVATE dkcomp)

add_executable(decomp decomp_util.c)
target_link_libraries(decomp PRIVATE dkcomp)

find_package(PkgConfig)

if(PkgConfig_FOUND)
  pkg_check_modules(MHD libmicrohttpd)
endif()

if(MHD_FOUND)
  add_executable(server server.c)
  configure_file(
    ${CMAKE_SOURCE_DIR}/server.html
    ${CMAKE_BINARY_DIR}/server.html
    COPYONLY
  )
  target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(server PRIVATE dkcomp microhttpd)
endif()
