dkcomp
======

dkcomp is a library for various Donkey Kong compression schemes.

Supported Formats
-----------------

* Big Data ............ SNES DKC2 and DKC3 tilesets, tilemaps and metatiles
* Small Data .......... SNES DKC3 tilemaps
* DKC CHR ............. SNES DKC tilesets
* DKC GBC ............. GBC DKC tilesets
* DKL Layout .......... GBx DKL series tilemaps
* DKL Huffman ......... GBx DKL series tilesets
* GBA LZ77 ............ GBA BIOS LZ77 (type-30)
* GBA RLE ............. GBA BIOS RLE (type-10)
* GBA Huffman 20 ...... GBA BIOS Huffman (type-20) (8-bit only)
* GBA Huffman 50 ...... GBA Inline Huffman (type-50)
* GBA Huffman 50 ...... GBA Inline Huffman (type-60)

Usage
-----

Two basic command line utilities are provided for compression and decompression (comp and decomp). A more convenient version with a simple web interface using libmicrohttpd is also provided.

Someone wishing to use this in their own software (such as a level editor) is encouraged to build the library, link against it and use the API found in "dkcomp.h".

Note: The DKL Huffman tileset format requires a few extra parameters, so those functions aren't currently accessible through the provided utilities or the standard API. Someone wishing to use them would need to call them directly.

Build Instructions
------------------

The project is written entirely in C, so a suitable C compiler is required. Just download the repository and build with [meson](https://mesonbuild.com/Quick-guide.html).

The library and CLI utilities have no dependencies. The web interface program requires libmicrohttpd.

License
-------
MIT

