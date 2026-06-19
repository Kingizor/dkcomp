# 'make'        - comp and decomp utilities
# 'make server' - libmicrohttpd version
# 'make wasm'   - WebAssembly version

CFLAGS       = -Wall -Wextra -O2 -fPIC
LDFLAGS      = -s -flto -L. -ldkcomp
WASM_CFLAGS  = --target=wasm32 -nostdlib -DBUILD_WASM -O2 -fPIC -Wall -Wextra
WASM_LDFLAGS = --target=wasm32 -s -flto -Wl,--no-entry

LIB_FILES = \
	src/dk_error.o \
	src/bigdata_comp.o \
	src/bigdata_decomp.o \
	src/smalldata.o \
	src/dkcchr.o \
	src/dkcgbc.o \
	src/dkl_tilemap.o \
	src/dkl_tileset.o \
	src/gbahuff20.o \
	src/gbahuff50.o \
	src/gbahuff60.o \
	src/gba_lz77.o \
	src/gba_rle.o \
	src/gba_auto.o \
	src/gb_printer.o \
	src/dk_comp_lib.o
WASM_FILES   = src/dk_wasm_libc.o src/dk_wasm_api.o src/qsort.o
COMP_FILES   = src/comp_util.o
DECOMP_FILES = src/decomp_util.o
SERVER_FILES = src/server.o

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
decomp: dkcomp src/decomp_util.o dkcomp
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(DECOMP_OUTPUT) $(DECOMP_FILES)
comp: dkcomp src/comp_util.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(COMP_OUTPUT) $(COMP_FILES)
server: dkcomp $(SERVER_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(SERVER_OUTPUT) $(SERVER_FILES) -lmicrohttpd
	cp src/server.html server.html

wasm: CC 	  = clang
wasm: CFLAGS  = $(WASM_CFLAGS)
wasm: LDFLAGS = $(WASM_LDFLAGS)
wasm: $(LIB_FILES) $(WASM_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(WASM_OUTPUT) $(LIB_FILES) $(WASM_FILES)
	cp src/wasm.html wasm.html

clean:
	rm -f $(LIB_FILES) $(LIB_OUTPUT)
	rm -f $(COMP_FILES) $(COMP_OUTPUT)
	rm -f $(DECOMP_FILES) $(DECOMP_OUTPUT)
	rm -f $(SERVER_FILES) $(SERVER_OUTPUT) server.html
	rm -f $(WASM_FILES) $(WASM_OUTPUT) wasm.html
