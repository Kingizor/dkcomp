project('dkcomp', 'c', license: 'MIT')

# library
dkc_common = [
  'src/dk_comp_lib.c',
  'src/dk_error.c',
  'src/bigdata_comp.c',
  'src/bigdata_decomp.c',
  'src/smalldata.c',
  'src/dkcchr.c',
  'src/dkcgbc.c',
  'src/dkl_tilemap.c',
  'src/dkl_tileset.c',
  'src/gbahuff20.c',
  'src/gbahuff50.c',
  'src/gbahuff60.c',
  'src/gba_lz77.c',
  'src/gba_rle.c',
  'src/gba_auto.c',
  'src/gb_printer.c'
]

libdkcomp = shared_library(
  'dkcomp',
  dkc_common,
  gnu_symbol_visibility: 'hidden'
)
depdkcomp = declare_dependency(link_with: libdkcomp, include_directories: '.')

# standalone utilities
executable(  'comp',   'src/comp_util.c', link_with: libdkcomp)
executable('decomp', 'src/decomp_util.c', link_with: libdkcomp)

# web version
mhttpd = dependency('libmicrohttpd', required: false)
if mhttpd.found()
  html = configure_file(
     input: 'src/server.html',
    output: 'server.html',
      copy:  true
  )
  executable('server', 'src/server.c', link_with: libdkcomp, dependencies: mhttpd)
endif

