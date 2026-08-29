CC      ?= cc
EMCC    ?= emcc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L \
	-D_XOPEN_SOURCE=700

NATIVE  = build/verb-not-found
WEB_JS  = public/game/verb-not-found.js
WEB_WASM = public/game/verb-not-found.wasm

$(NATIVE): native/game.c native/messages.h
	mkdir -p build
	$(CC) $(CFLAGS) native/game.c -o $(NATIVE)

native: $(NATIVE)

test: $(NATIVE)
	$(NATIVE) -t >/dev/null
	$(NATIVE) -T >/dev/null
	@echo "native walkthroughs: ok"

$(WEB_JS) $(WEB_WASM) &: native/game.c native/messages.h
	mkdir -p public/game
	$(EMCC) $(CFLAGS) -Wno-unused-function native/game.c --no-entry \
		-sMODULARIZE=1 \
		-sEXPORT_NAME=createVerbNotFoundModule \
		-sENVIRONMENT=web \
		-sFILESYSTEM=0 \
		-sNO_EXIT_RUNTIME=1 \
		-sALLOW_MEMORY_GROWTH=1 \
		-sEXPORTED_RUNTIME_METHODS=cwrap \
		-sEXPORTED_FUNCTIONS=_web_game_init,_web_game_command,_web_game_ended,_malloc,_free \
		-o $(WEB_JS)

web: $(WEB_JS) $(WEB_WASM)

site: test web
	npm run build

clean:
	rm -rf build dist
	rm -f public/game/verb-not-found.js public/game/verb-not-found.wasm

.PHONY: native test web site clean
