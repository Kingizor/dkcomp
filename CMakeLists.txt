cmake_minimum_required(VERSION 3.10)

project(dkcomp)

set(  comp_src   bd_comp_core.c   sd_comp_core.c)
set(decomp_src bd_decomp_core.c sd_decomp_core.c dkcchr_decomp_core.c dkcgbc_decomp_core.c dkl_decomp_core.c)
set(dkcommon dk_comp_lib.c dk_internal.h ${comp_src} ${decomp_src})

option(BUILD_COMP   "Build the standalone compressor"   ON)
option(BUILD_DECOMP "Build the standalone decompressor" ON)
option(BUILD_DKCOMP "Build the dkcomp library"          ON)

if (BUILD_DKCOMP)
    add_library(dkcomp SHARED ${dkcommon})
    set_target_properties(dkcomp PROPERTIES PUBLIC_HEADER dkcomp.h)
endif()

if (BUILD_COMP)
    add_executable(comp dk_comp_util.c)
    target_link_libraries(comp dkcomp)
endif()
if (BUILD_DECOMP)
    add_executable(decomp dk_decomp_util.c)
    target_link_libraries(decomp dkcomp)
endif()
