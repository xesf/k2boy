# k2boy — convenience wrapper around CMake.
#
#   make play       build optimised and play the game: boots like the original
#                   (startup module -> prisonfix), with every feature on —
#                   animation, collision, trap scripts, crates, doors, plates.
#                   Options: CONTROLS=modern|classic (default modern)
#                            REGION=n  start region (default 0)
#                            TRACE=1   print every trap message
#                            ARGS=...  extra flags passed straight through
#   make play-debug same, from the Debug build
#
#   Controls (C switches scheme in game):
#     Modern   WASD / left stick move relative to the camera; mouse drag or
#              right stick turns the camera. A full stick push runs.
#     Classic  the original's tank controls: W/S walk forward/back along his
#              facing, A/D turn him; the camera stays behind him.
#     Both     Shift / RB / RT run, Space / A jump, E / X act (grab a crate and
#              hold: then forward pushes, back pulls).
#     Ledges   E facing a ledge he can reach climbs onto it; jump under a high
#              one to catch it, then up pulls up, down lets go, left/right
#              shimmy.
#     Combat   E picks up (the sword is in prison4); R / pad Y draws or sheathes,
#              F / pad B attacks (left / right / centre by the stick), G / pad LB
#              blocks. The bottles bottom left are health (three to start);
#              a fallen Prince returns to the level start.
#     Talk     an E badge shows where E does something: talk to prisoners,
#              pull levers, use things.
#     Swim     in deep water: move to swim, E dives, Space rises (air bottle
#              shows under water), jump or E at a low edge to climb out.
#     Also     V free-fly camera, T trigger volumes, Esc quit. Cutscenes take
#              all control until they end. A gamepad (Xbox / PlayStation / MFi)
#              is picked up when connected.
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
#   --autowalk / --autorun drive the Prince headlessly (with --frames N --shot)
#   --noclip disables collision, --noanim holds the bind pose
#
#   In the viewer: F toggles follow-the-Prince vs free-fly.
#   In the viewer: WASD move, Q/E or Space up/down, drag LMB to look,
#   scroll to change speed, Shift fast, Ctrl slow, R reframe,
#   T toggle trigger volumes, Esc quit.
#   make actors     headless: parse every .act, report skeletons
#   make skills     headless: parse every .skl, validate quaternions
#   make spawns     headless: resolve every PrinceStart/EnemyStart to a body
#   make bind A=... headless: compare .act skeleton against .K2A bind pose
#   make actnodes A=... headless: dump .act node records and their surroundings
#   make materials  headless: resolve every material's texture across all .K2A
#   make winding    headless: measure .K2A triangle winding (gates backface cull)
#   make skeletons  headless: recover and validate every .act skeleton tree
#   make config     headless: parse distribution.dat the way WinMain does
#   make modules    headless: print the module table and check its ordering
#   make boot       launch the way WinMain does: startup module -> level
#   make shot S=... render one frame to a .bmp and exit (add --cam/--notrig)
#                   --revdraw reverses opaque order; frame must not change
#   make levels     headless: parse every .lvl and check all paths resolve
#   make check      verify + images + parse every .K2A  (no window needed)
#   make release    optimised build
#   make coverage   regenerate docs/reference/coverage.md
#   make clean      remove the build directory
#
#   web/ — the WebGPU experiment (docs/plan/06-web-experiment.md)
#   make web        generate the manifest and serve the repo at :8080
#   make webindex   regenerate web/data-index.txt from the install

BUILD     ?= build
TYPE      ?= Debug
DATA      ?= data
BIN        = $(BUILD)/k2boy
RBUILD     = $(BUILD)-release
RBIN       = $(RBUILD)/k2boy
JOBS      ?= $(shell sysctl -n hw.ncpu 2>/dev/null || echo 4)

REGION    ?= 0
CONTROLS  ?= modern
TRACE     ?= 0
ARGS      ?=
PLAY_FLAGS = --boot --notrig --controls $(CONTROLS) --region $(REGION) $(if $(filter 1,$(TRACE)),--trapmsg) $(ARGS)

# Configures a build directory, or reconfigures it when its cache points at a
# CMake that no longer exists (a CMake upgrade otherwise breaks every target
# with "cmake: No such file or directory").
#   $(call configure,<dir>,<build type>)
define configure
	@c="$(1)/CMakeCache.txt"; \
	if [ -f "$$c" ]; then \
	    cm=$$(sed -n 's/^CMAKE_COMMAND:INTERNAL=//p' "$$c"); \
	    if [ ! -x "$$cm" ]; then \
	        echo "k2boy: $(1) was configured with a CMake that is gone ($$cm); reconfiguring"; \
	        rm -rf "$$c" "$(1)/CMakeFiles"; \
	    fi; \
	fi; \
	[ -f "$$c" ] || cmake -B "$(1)" -DCMAKE_BUILD_TYPE=$(2) >/dev/null
endef

# The retail install is not in the repository; say so instead of failing later.
define need_data
	@test -d "$(DATA)" || { echo "k2boy: no game data at '$(DATA)'. Copy the retail install there, or pass DATA=<path>."; exit 1; }
endef

DEFAULT_ROOM ?= geometry\\rooms\\prisonfix\\prison1.k2a

.PHONY: all play play-debug configure build run verify images dump room level levels actor prince actors skills spawns shot bind actnodes materials winding skeletons config modules boot skl check release coverage web webindex clean rebuild help

all: build

configure:
	$(call configure,$(BUILD),$(TYPE))

build: configure
	@cmake --build $(BUILD) -j $(JOBS)

release:
	$(call configure,$(RBUILD),Release)
	@cmake --build $(RBUILD) -j $(JOBS)

# The game, as far as it goes: the boot sequence with everything enabled.
play: release
	$(need_data)
	@./$(RBIN) $(PLAY_FLAGS) $(DATA)

play-debug: build
	$(need_data)
	@./$(BIN) $(PLAY_FLAGS) $(DATA)

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

shot: build
	@$(BIN) --room "$(DEFAULT_ROOM)" --shot "$(S)" $(DATA)

boot: build
	@$(BIN) --boot $(DATA)

modules: build
	@$(BIN) --modules $(DATA)

skeletons: build
	@$(BIN) --skeletons $(DATA)

winding: build
	@$(BIN) --winding $(DATA)

materials: build
	@$(BIN) --materials $(DATA)

config: build
	@$(BIN) --config $(DATA)

actnodes: build
	@$(BIN) --actnodes "$(A)" $(DATA)

bind: build
	@$(BIN) --bind "$(A)" $(DATA)

spawns: build
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

# --- web ---------------------------------------------------------------------
#
# The browser cannot list a directory, so the VFS index that vfs.cpp builds with
# opendir has to be generated ahead of time. Everything else is static: no
# bundler, no transpiler, no node_modules.

WEB_PORT ?= 8080

webindex:
	@python3 tools/webindex.py $(DATA) web/data-index.txt

web: webindex
	@echo "serving http://localhost:$(WEB_PORT)/web/  (needs a WebGPU browser)"
	@python3 -m http.server $(WEB_PORT) --bind 127.0.0.1

clean:
	@rm -rf $(BUILD) $(RBUILD)

rebuild: clean build

help:
	@grep -E '^#   ' Makefile | sed 's/^#   //'
