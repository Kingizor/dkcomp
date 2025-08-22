project('dkcomp', 'c', license: 'MIT')

# library
dkc_common = [
  'dk_comp_lib.c',
  'dk_error.c',
  'bigdata_comp.c',
  'bigdata_decomp.c',
  'smalldata.c',
  'dkcchr.c',
  'dkcgbc.c',
  'dkl_tilemap.c',
  'dkl_tileset.c',
  'gbahuff20.c',
  'gbahuff50.c',
  'gbahuff60.c',
  'gba_lz77.c',
  'gba_rle.c',
  'gba_auto.c',
  'gb_printer.c'
]

libdkcomp = shared_library(
  'dkcomp',
  dkc_common,
  gnu_symbol_visibility: 'hidden'
)
depdkcomp = declare_dependency(link_with: libdkcomp, include_directories: '.')

# standalone utilities
executable(  'comp',   'comp_util.c', link_with: libdkcomp)
executable('decomp', 'decomp_util.c', link_with: libdkcomp)

# web version
mhttpd = dependency('libmicrohttpd', required: false)
if mhttpd.found()
  html = configure_file(
     input: 'server.html',
    output: 'server.html',
      copy:  true
  )
  executable('server', 'server.c', link_with: libdkcomp, dependencies: mhttpd)
endif

