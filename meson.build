project('dkcomp', 'c', license: 'MIT')

# library
dkc_common = [
  'dk_comp_lib.c',
  'dk_error.c',
  'bd_comp_core.c',
  'bd_decomp_core.c',
  'sd_comp_core.c',
  'dkcchr_decomp_core.c',
  'dkcgbc_decomp_core.c',
  'dkl_decomp_core.c',
  'gbahuff20.c',
  'gbahuff50.c',
  'gbahuff60.c',
  'gba_lz77.c',
  'gba_rle.c',
  'gba.c'
]

libdkcomp = shared_library('dkcomp', dkc_common, gnu_symbol_visibility: 'hidden')

# standalone utilities
executable(      'comp',   'dk_comp_util.c', link_with: libdkcomp)
executable(    'decomp', 'dk_decomp_util.c', link_with: libdkcomp)

