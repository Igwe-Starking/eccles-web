# ECCLES WEB - host build
CC      ?= gcc
CSTD    ?= -std=c99
WARN    := -Wall -Wextra
OPT     ?= -O2
INC     := -Isrc -Iconfig
SRC     := src/eccles_web.c
B       := build

.PHONY: all examples san size clean
all: examples

$(B):
	mkdir -p $(B)

examples: $(B)/example_host
$(B)/example_host: examples/host/main.c $(SRC) src/eccles_web.h | $(B)
	$(CC) $(CSTD) $(WARN) $(OPT) $(INC) examples/host/main.c $(SRC) -o $@ -lm

san: | $(B)
	$(CC) $(CSTD) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(INC) examples/host/main.c $(SRC) -o $(B)/example_san -lm
	$(B)/example_san

size: | $(B)
	@for f in "" "-DECCLES_WEB_JS=0"; do \
	  $(CC) $(CSTD) -Os $(INC) -DECCLES_WEB_PLATFORM=ECCLES_WEB_PLAT_NONE -DECCLES_WEB_ARENA_SIZE=131072ul $$f -c $(SRC) -o $(B)/size.o; \
	  echo "[$$f]"; size $(B)/size.o; \
	done

clean:
	rm -rf $(B)
