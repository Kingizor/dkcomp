# 'make'        - comp and decomp utilities
# 'make server' - libmicrohttpd version
# 'make wasm'   - WebAssembly version

CFLAGS       = -Wall -Wextra -O2 -fPIC
LDFLAGS      = -s -flto -L. -ldkcomp
WASM_CFLAGS  = --target=wasm32 -nostdlib -DBUILD_WASM -O2 -fPIC -Wall -Wextra
WASM_LDFLAGS = --target=wasm32 -s -flto -Wl,--no-entry

LIB_FILES = \
	dk_error.o \
	bigdata_comp.o \
	bigdata_decomp.o \
	smalldata.o \
	dkcchr.o \
	dkcgbc.o \
	dkl_tilemap.o \
	dkl_tileset.o \
	gbahuff20.o \
	gbahuff50.o \
	gbahuff60.o \
	gba_lz77.o \
	gba_rle.o \
	gba_auto.o \
	gb_printer.o \
	dk_comp_lib.o
WASM_FILES   = dk_wasm_libc.o dk_wasm_api.o qsort.o
COMP_FILES   = comp_util.o
DECOMP_FILES = decomp_util.o
SERVER_FILES = server.o

ifeq ($(OS),Windows_NT)
	LIB_EXT  = .dll
	EXE_EXT  = .exe

else
	LIB_EXT  = .so
	LDFLAGS += -Wl,-rpath,'$$ORIGIN'
endif

LIB_OUTPUT    = libdkcomp$(LIB_EXT)
COMP_OUTPUT   = comp$(EXE_EXT)
DECOMP_OUTPUT = decomp$(EXE_EXT)
SERVER_OUTPUT = server$(EXE_EXT)
WASM_OUTPUT   = dkcomp.wasm

all: comp decomp

dkcomp: $(LIB_FILES)
	$(CC) -s -flto -shared -o $(LIB_OUTPUT) $(LIB_FILES)
decomp: dkcomp decomp_util.o dkcomp
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(DECOMP_OUTPUT) $(DECOMP_FILES)
comp: dkcomp comp_util.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(COMP_OUTPUT) $(COMP_FILES)
server: dkcomp $(SERVER_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(SERVER_OUTPUT) $(SERVER_FILES) -lmicrohttpd

wasm: CC 	  = clang
wasm: CFLAGS  = $(WASM_CFLAGS)
wasm: LDFLAGS = $(WASM_LDFLAGS)
wasm: $(LIB_FILES) $(WASM_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(WASM_OUTPUT) $(LIB_FILES) $(WASM_FILES)

clean:
	rm -f $(LIB_FILES) $(LIB_OUTPUT) $(COMP_FILES) $(COMP_OUTPUT) $(DECOMP_FILES) $(DECOMP_OUTPUT) $(SERVER_FILES) $(SERVER_OUTPUT) $(WASM_FILES) $(WASM_OUTPUT)
