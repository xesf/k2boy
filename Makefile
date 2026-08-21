# k2boy — convenience wrapper around CMake.
#
#   make            build (Debug)
#   make run        build and launch the default room
#   make verify     headless: mount the VFS and probe awkward paths
#   make images     headless: decode every BMP/TGA in the install
#   make dump F=... headless: parse one .K2A and report
#   make room R=... launch a specific room
#   make level L=... launch a whole level (.lvl)
#   make actor A=... render a character from its master.dat
#   make prince     render the Prince (shortcut)
#
#   In the viewer: WASD move, Q/E or Space up/down, drag LMB to look,
#   scroll to change speed, Shift fast, Ctrl slow, R reframe,
#   T toggle trigger volumes, Esc quit.
#   make actors     headless: parse every .act, report skeletons
#   make skills     headless: parse every .skl, validate quaternions
#   make spawns     headless: resolve every PrinceStart/EnemyStart to a body
#   make shot S=... render one frame to a .bmp and exit (add --cam/--notrig)
#   make levels     headless: parse every .lvl and check all paths resolve
#   make check      verify + images + parse every .K2A  (no window needed)
#   make release    optimised build
#   make coverage   regenerate docs/reference/coverage.md
#   make clean      remove the build directory

BUILD     ?= build
TYPE      ?= Debug
DATA      ?= data
BIN        = $(BUILD)/k2boy
JOBS      ?= $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)

DEFAULT_ROOM ?= geometry\\rooms\\prisonfix\\prison1.k2a

.PHONY: all build run verify images dump room level levels actor prince actors skills spawns shot skl check release coverage clean rebuild help

all: build

$(BUILD)/CMakeCache.txt:
	@cmake -B $(BUILD) -DCMAKE_BUILD_TYPE=$(TYPE)

build: $(BUILD)/CMakeCache.txt
	@cmake --build $(BUILD) -j $(JOBS)

release:
	@cmake -B $(BUILD)-release -DCMAKE_BUILD_TYPE=Release
	@cmake --build $(BUILD)-release -j $(JOBS)

run: build
	@./$(BIN) --room "$(DEFAULT_ROOM)" $(DATA)

# make room R="Geometry/Rooms/Cistern/cistern.k2a"
room: build
	@test -n "$(R)" || { echo 'usage: make room R=<path within data/>'; exit 1; }
	@./$(BIN) --room "$(R)" $(DATA)

# make level L="Geometry/Rooms/prisonfix/prisonfix.lvl"
level: build
	@test -n "$(L)" || { echo 'usage: make level L=<path within data/>'; exit 1; }
	@./$(BIN) --level "$(L)" $(DATA)

levels: build
	@find $(DATA) -iname '*.lvl' -print0 | while IFS= read -r -d '' f; do \
	    rel=$${f#$(DATA)/}; \
	    line=$$(./$(BIN) --dumplevel "$$rel" $(DATA) 2>&1 | grep 'unresolved:'); \
	    printf "  %-34s %s\n" "$$(basename "$$f")" "$$line"; \
	done

# make actor A="geometry/enemies/pg1/master.dat"
actor: build
	@test -n "$(A)" || { echo 'usage: make actor A=<master.dat within data/>'; exit 1; }
	@./$(BIN) --actor "$(A)" $(DATA)

prince: build
	@./$(BIN) --actor "geometry/prince/master.dat" $(DATA)

actors: build
	@./$(BIN) --actors $(DATA) | tail -4

shot: $(BIN)
	@$(BIN) --room "$(DEFAULT_ROOM)" --shot "$(S)" $(DATA)

spawns: $(BIN)
	@$(BIN) --spawns

skills: build
	@./$(BIN) --skills $(DATA) | tail -4

# make skl F="Actors/Prince/Skills/Running_L.skl"
skl: build
	@test -n "$(F)" || { echo 'usage: make skl F=<path within data/>'; exit 1; }
	@./$(BIN) --skl "$(F)" $(DATA)

verify: build
	@./$(BIN) --verify $(DATA)

images: build
	@./$(BIN) --images $(DATA)

# make dump F="Actors/PICK_Gem/PICK_Gem.K2A"
dump: build
	@test -n "$(F)" || { echo 'usage: make dump F=<path within data/>'; exit 1; }
	@./$(BIN) --dump "$(F)" $(DATA)

# Everything that can run without a window. Parses every .K2A in the install;
# the three known failures are neighbour files carrying a .k2a extension
# (cbors, ivneighbors, d2neighbors) — see docs/formats/lvl.md.
check: build
	@echo "== VFS =="
	@./$(BIN) --verify $(DATA) | tail -n +1
	@echo
	@echo "== images =="
	@./$(BIN) --images $(DATA) | grep -E '^  \.|^images:'
	@echo
	@echo "== levels =="
	@$(MAKE) --no-print-directory levels
	@echo
	@echo "== geometry =="
	@ok=0; fail=0; \
	for f in $$(find $(DATA) -iname '*.k2a' | sed 's|^$(DATA)/||'); do \
	    if ./$(BIN) --dump "$$f" $(DATA) 2>&1 | grep -q 'parse failed'; then \
	        fail=$$((fail+1)); echo "  FAIL $$f"; \
	    else ok=$$((ok+1)); fi; \
	done; \
	echo "  parsed $$ok, failed $$fail"

coverage:
	@python3 tools/coverage.py

clean:
	@rm -rf $(BUILD) $(BUILD)-release

rebuild: clean build

help:
	@grep -E '^#   ' Makefile | sed 's/^#   //'
