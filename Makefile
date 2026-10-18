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
TEST_test_sleep_opts_SRCS := apps/sleep/sleep_opts.c
TEST_test_sha256_SRCS := core/kui/sha256.c
TEST_test_fw_SRCS := apps/firewall/fw.c
TEST_test_sshcfg_SRCS := apps/ssh/sshcfg.c core/kui/sha256.c
TEST_test_sshsrv_SRCS := apps/ssh/sshsrv.c
TEST_test_keyboard_SRCS := core/kui/keyboard.c core/kui/layout.c core/kui/utf8tr.c
TEST_test_http_SRCS := core/net/http.c
TEST_test_xml_SRCS := core/net/xml.c core/kui/utf8tr.c
TEST_test_wrap_SRCS := core/kui/wrap.c core/kui/utf8tr.c
TEST_test_trfmt_SRCS := core/kui/trfmt.c
TEST_test_wx_SRCS := apps/weather/wx.c core/net/http.c core/vendor/cjson/cJSON.c core/kui/store.c core/kui/trfmt.c core/kui/utf8tr.c
TEST_test_lipc_SRCS := core/kui/lipc.c core/kui/store.c
TEST_test_fx_SRCS := apps/currency/fx.c core/net/xml.c core/kui/utf8tr.c core/vendor/cjson/cJSON.c
TEST_test_news_SRCS := apps/news/news.c core/net/xml.c core/kui/utf8tr.c core/kui/trfmt.c
TEST_test_uci_SRCS := apps/chess/uci.c apps/chess/levels.c

TESTS := $(patsubst tests/host/%.c,%,$(wildcard tests/host/test_*.c))

build/host/%: tests/host/%.c tests/host/t.h $(wildcard core/kui/*.h) $(wildcard apps/*/*.h) $(wildcard core/net/*.h) $$(TEST_$$*_SRCS)
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

# NET_SRCS: linked into apps that fetch data (curl HTTP, XML text, cJSON)
NET_SRCS := core/net/http.c core/net/xml.c core/vendor/cjson/cJSON.c

# APPS: each entry <name> builds $(OUT)/<name> from APP_<name>_SRCS + kui + FBInk
APPS := hub game2048 chess sleep firewall ssh weather clock currency news
APP_weather_SRCS := apps/weather/main.c apps/weather/wx.c apps/weather/wxdraw.c $(NET_SRCS)
APP_clock_SRCS := apps/clock/main.c apps/weather/wx.c apps/weather/wxdraw.c $(NET_SRCS)
APP_currency_SRCS := apps/currency/main.c apps/currency/fx.c $(NET_SRCS)
APP_news_SRCS := apps/news/main.c apps/news/news.c $(NET_SRCS)
APP_ssh_SRCS := apps/ssh/main.c apps/ssh/sshcfg.c apps/ssh/sshsrv.c
APP_firewall_SRCS := apps/firewall/main.c apps/firewall/fw.c apps/ssh/sshsrv.c
APP_sleep_SRCS := apps/sleep/main.c apps/sleep/sleep_opts.c
APP_chess_SRCS := apps/chess/main.c apps/chess/chess_game.c apps/chess/chess_rules.c apps/chess/levels.c apps/chess/uci.c
APP_hub_SRCS := core/hub/main.c core/hub/appreg.c
APP_game2048_SRCS := apps/game2048/main.c apps/game2048/g2048.c

$(OUT)/%: $$(APP_$$*_SRCS) $(FBINK_A) $(KUI_SRCS) $(wildcard core/kui/*.h)
	@mkdir -p $(dir $@)
	$(CROSS_CC) $(CROSS_CFLAGS) -o $@ $(APP_$*_SRCS) $(KUI_SRCS) $(FBINK_A) -lm
	$(CROSS_TC)-strip $@

cross: $(FBINK_A) $(addprefix $(OUT)/,$(APPS)) $(OUT)/stockfish $(OUT)/dropbear

$(OUT)/dropbear: tools/build-dropbear.sh tools/patches/dropbear-apppass.patch apps/ssh/sshcfg.c core/kui/sha256.c
	./tools/build-dropbear.sh

$(OUT)/stockfish: tools/build-stockfish.sh
	./tools/build-stockfish.sh

build:
	[ -d third_party/FBInk ] || ./tools/fetch-deps.sh
	docker run --rm -v "$(CURDIR)":/src $(DOCKER_IMG) make cross

clean:
	rm -rf build dist

.PHONY: test cross build clean

# ---- packaging: stage each package under build/pkg/<id>, then tar it into dist/ ----

# PKGS: each <appdir> under apps/ with a pkg/ dir; PKG_<appdir>_FILES go to files/ (installed next to app.ini)
PKGS := game2048 chess sleep ssh firewall
PKG_game2048_FILES := $(OUT)/game2048
PKG_chess_FILES := $(OUT)/chess $(OUT)/stockfish $(OUT)/COPYING-stockfish
PKG_sleep_FILES := $(OUT)/sleep
PKG_ssh_FILES := $(OUT)/ssh $(OUT)/dropbear $(OUT)/dropbearkey $(OUT)/LICENSE-dropbear
PKG_firewall_FILES := $(OUT)/firewall

dist:
	@for f in $(OUT)/hub $(foreach p,$(PKGS),$(PKG_$(p)_FILES)); do [ -f $$f ] || { echo "missing $$f: run make build first"; exit 1; }; done
	rm -rf build/pkg dist
	mkdir -p build/pkg/core/files/bin build/pkg/core/files/fonts
	cp core/pkg/manifest.json core/pkg/install.sh core/pkg/uninstall.sh core/pkg/launch.sh build/pkg/core/
	cp -R core/pkg/scriptlets build/pkg/core/
	cp core/kui-run.sh $(OUT)/hub build/pkg/core/files/bin/
	cp third_party/fonts/DejaVuSans.ttf third_party/fonts/DejaVuSans-Bold.ttf third_party/fonts/DejaVu-LICENSE build/pkg/core/files/fonts/
	./tools/pack.sh build/pkg/core dist
	@set -e; $(foreach p,$(PKGS),./tools/stage-app.sh $(p) build/pkg/$(p) $(PKG_$(p)_FILES); ./tools/pack.sh build/pkg/$(p) dist;)
	./tools/gen-manifest.sh dist
	./tests/pkg/test_pack.sh

KINDLE ?= 192.168.2.90
PORT   ?= 22
SSH    := sshpass -p '' ssh -o PubkeyAuthentication=no -p $(PORT) root@$(KINDLE)

# Installs dist packages the way kpm does: unpack into a staging dir and run install.sh there (core first).
deploy:
	@for id in kindle-apps-core kindle-apps-2048 kindle-apps-chess kindle-apps-sleep kindle-apps-ssh kindle-apps-firewall; do \
		k=$$(ls dist/packages/$$id/*.kpkg | tail -1); echo "deploy $$k"; \
		cat $$k | $(SSH) "S=/mnt/us/kindle-apps-staging/$$id; rm -rf \$$S; mkdir -p \$$S && cd \$$S && tar xzf - && sh ./install.sh && cd / && rm -rf \$$S && echo installed $$id" || exit 1; \
	done

.PHONY: dist deploy

# ---- host simulator: build/sim/<app> renders PNGs (tools/sim/sim.c); run with tools/sim/run.sh ----
SIM_KUI_SRCS := $(filter-out core/kui/screen.c core/kui/input.c,$(KUI_SRCS)) tools/sim/sim.c
build/sim/%: $$(APP_$$*_SRCS) $(SIM_KUI_SRCS) $(wildcard core/kui/*.h) $(wildcard apps/*/*.h)
	@mkdir -p $(dir $@) build/simroot/fonts
	cp -n third_party/fonts/*.ttf build/simroot/fonts/ 2>/dev/null || true
	$(HOST_CC) -std=c11 -g -O1 -fsanitize=address,undefined -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
		-Icore/kui -Iapps -Icore -Itools/sim -o $@ $(APP_$*_SRCS) $(SIM_KUI_SRCS) -lz -lm
