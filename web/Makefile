# The web build: the emulator core as a WebAssembly module (Emscripten), the
# page that drives it and the game extensions, gathered in dist/ ready to be
# served as static files.
#
#   make            build dist/
#   make serve      serve dist/ on http://localhost:8800
#   make clean

SRC = ../src
OUT = dist
OBJ = obj

CORE = acidtest afile antic artifact atari binload cartridge cartridge_info cassette cfg \
       colours colours_external colours_ntsc colours_pal compfile cpu crc32 cycle_map devices \
       esc file_export gtia img_tape input log memory monitor mzpokeysnd pbi pia pokey pokeysnd remez rtime \
       screen sio sound statesav sysrom util ext-cpu \
       roms/altirra_5200_os roms/altirra_5200_charset roms/altirraos_800 roms/altirraos_xl roms/altirra_basic \
       web/web

OBJS = $(CORE:%=$(OBJ)/%.o)
CFLAGS = -O2 -I$(SRC)/web -I$(SRC) -DHAVE_CONFIG_H -DWITH_EXT=1 -Wno-deprecated-non-prototype -sUSE_ZLIB=1
LDFLAGS = -O2 -sUSE_ZLIB=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node -sEXPORT_NAME=createAtari800 \
          -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=67108864 -sEXIT_RUNTIME=0 -sINVOKE_RUN=0 \
          -sEXPORTED_RUNTIME_METHODS=callMain,FS,HEAPU8,HEAP32,HEAPF64,stringToNewUTF8 -sEXPORTED_FUNCTIONS=_main,_malloc,_free

PAGE = index.html host.js api.js glshim.js std.js os.js

all: $(OUT)/atari800.js page

$(OUT)/atari800.js: $(OBJS)
	@mkdir -p $(OUT)
	emcc $(LDFLAGS) $(OBJS) -o $@

$(OBJ)/%.o: $(SRC)/%.c $(SRC)/web/config.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) -c $< -o $@

# The page, and the extensions as the native build has them
page:
	@mkdir -p $(OUT)
	cp $(PAGE) $(OUT)/
	rm -rf $(OUT)/ext && cp -R ../data/ext $(OUT)/ext
	rm -f $(OUT)/ext/altreal/*.pdf $(OUT)/ext/altreal/*.atr; rm -rf $(OUT)/ext/altreal/maps
	cd $(OUT) && find ext -type f | sort > files.txt

serve: all
	cd $(OUT) && python3 -m http.server 8800

clean:
	rm -rf $(OBJ) $(OUT)

.PHONY: all page serve clean
