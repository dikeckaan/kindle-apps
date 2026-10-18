# Kindle Apps build.
#   make test               host unit tests (native cc)
#   make build              cross-compile device binaries in Docker (kindle-apps-tc image)
#   make dist               .kpkg packages + kpm repo manifest
#   make deploy KINDLE=ip   install dist packages on a device over SSH

HOST_CC     ?= cc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-omit-frame-pointer -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -Icore/kui -Iapps -Icore
DOCKER_IMG  := kindle-apps-tc

.SECONDEXPANSION:

# ---- host tests: TEST_<name>_SRCS lists the sources each test links against ----
TEST_test_smoke_SRCS :=
TEST_test_refresh_SRCS := core/kui/refresh.c
TEST_test_layout_SRCS := core/kui/layout.c
TEST_test_utf8tr_SRCS := core/kui/utf8tr.c
TEST_test_store_SRCS := core/kui/store.c
TEST_test_gesture_SRCS := core/kui/gesture.c
TEST_test_g2048_SRCS := apps/game2048/g2048.c
TEST_test_appreg_SRCS := core/hub/appreg.c
TEST_test_chess_rules_SRCS := apps/chess/chess_rules.c
TEST_test_chess_game_SRCS := apps/chess/chess_game.c apps/chess/chess_rules.c
TEST_test_levels_SRCS := apps/chess/levels.c
TEST_test_uci_SRCS := apps/chess/uci.c apps/chess/levels.c

TESTS := $(patsubst tests/host/%.c,%,$(wildcard tests/host/test_*.c))

build/host/%: tests/host/%.c tests/host/t.h $(wildcard core/kui/*.h) $(wildcard apps/*/*.h) $$(TEST_$$*_SRCS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(TEST_$*_SRCS) -lm

test: $(addprefix build/host/,$(TESTS))
	@fail=0; for t in $(TESTS); do \
		if ./build/host/$$t; then echo "$$t: OK"; else echo "$$t: FAILED"; fail=1; fi; \
	done; exit $$fail

# ---- device build (runs inside Docker) ----
CROSS_TC ?= arm-kindlehf-linux-gnueabihf
CROSS_CC := $(CROSS_TC)-gcc
CROSS_CFLAGS := -std=c11 -O2 -Wall -Wextra -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -Icore/kui -Iapps -Icore -Ithird_party/FBInk
FBINK_A := third_party/FBInk/Release/libfbink.a
KUI_SRCS := $(wildcard core/kui/*.c)
OUT := build/kindlehf

$(FBINK_A):
	$(MAKE) -C third_party/FBInk staticlib KINDLE=1 CROSS_TC=$(CROSS_TC)

# APPS: each entry <name> builds $(OUT)/<name> from APP_<name>_SRCS + kui + FBInk
APPS := hub game2048 chess
APP_chess_SRCS := apps/chess/main.c apps/chess/chess_game.c apps/chess/chess_rules.c apps/chess/levels.c apps/chess/uci.c
APP_hub_SRCS := core/hub/main.c core/hub/appreg.c
APP_game2048_SRCS := apps/game2048/main.c apps/game2048/g2048.c

$(OUT)/%: $$(APP_$$*_SRCS) $(FBINK_A) $(KUI_SRCS) $(wildcard core/kui/*.h)
	@mkdir -p $(dir $@)
	$(CROSS_CC) $(CROSS_CFLAGS) -o $@ $(APP_$*_SRCS) $(KUI_SRCS) $(FBINK_A) -lm
	$(CROSS_TC)-strip $@

cross: $(FBINK_A) $(addprefix $(OUT)/,$(APPS)) $(OUT)/stockfish

$(OUT)/stockfish: tools/build-stockfish.sh
	./tools/build-stockfish.sh

build:
	[ -d third_party/FBInk ] || ./tools/fetch-deps.sh
	docker run --rm -v "$(CURDIR)":/src $(DOCKER_IMG) make cross

clean:
	rm -rf build dist

.PHONY: test cross build clean

# ---- packaging: stage each package under build/pkg/<id>, then tar it into dist/ ----

dist:
	@for b in hub game2048; do [ -x $(OUT)/$$b ] || { echo "missing $(OUT)/$$b: run make build first"; exit 1; }; done
	rm -rf build/pkg dist
	mkdir -p build/pkg/core/files/bin build/pkg/core/files/fonts build/pkg/2048/files
	cp core/pkg/manifest.json core/pkg/install.sh core/pkg/uninstall.sh core/pkg/launch.sh build/pkg/core/
	cp -R core/pkg/scriptlets build/pkg/core/
	cp core/kui-run.sh $(OUT)/hub build/pkg/core/files/bin/
	cp third_party/fonts/DejaVuSans.ttf third_party/fonts/DejaVuSans-Bold.ttf third_party/fonts/DejaVu-LICENSE build/pkg/core/files/fonts/
	cp apps/game2048/pkg/* build/pkg/2048/
	cp $(OUT)/game2048 build/pkg/2048/files/
	./tools/pack.sh build/pkg/core dist
	./tools/pack.sh build/pkg/2048 dist
	./tools/gen-manifest.sh dist
	./tests/pkg/test_pack.sh

KINDLE ?= 192.168.2.90
PORT   ?= 22
SSH    := sshpass -p '' ssh -o PubkeyAuthentication=no -p $(PORT) root@$(KINDLE)

# Installs dist packages the way kpm does: unpack into a staging dir and run install.sh there (core first).
deploy:
	@for id in kindle-apps-core kindle-apps-2048; do \
		k=$$(ls dist/packages/$$id/*.kpkg | tail -1); echo "deploy $$k"; \
		cat $$k | $(SSH) "S=/mnt/us/kindle-apps-staging/$$id; rm -rf \$$S; mkdir -p \$$S && cd \$$S && tar xzf - && sh ./install.sh && cd / && rm -rf \$$S && echo installed $$id" || exit 1; \
	done

.PHONY: dist deploy
