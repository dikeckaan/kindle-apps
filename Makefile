# Kindle Apps build.
#   make test               host unit tests (native cc)
#   make build              cross-compile device binaries in Docker (kindle-apps-tc image)
#   make dist               .kpkg packages + kpm repo manifest
#   make deploy KINDLE=ip   install dist packages on a device over SSH

HOST_CC     ?= cc
HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -g -D_DARWIN_C_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -Icore/kui -Iapps
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
CROSS_CFLAGS := -std=c11 -O2 -Wall -Wextra -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -Icore/kui -Iapps -Ithird_party/FBInk
FBINK_A := third_party/FBInk/Release/libfbink.a
KUI_SRCS := $(wildcard core/kui/*.c)
OUT := build/kindlehf

$(FBINK_A):
	$(MAKE) -C third_party/FBInk staticlib KINDLE=1 CROSS_TC=$(CROSS_TC)

# APPS: each entry <name> builds $(OUT)/<name> from APP_<name>_SRCS + kui + FBInk
APPS := kuidemo
APP_kuidemo_SRCS := apps/kuidemo/main.c

$(OUT)/%: $$(APP_$$*_SRCS) $(FBINK_A) $(KUI_SRCS) $(wildcard core/kui/*.h)
	@mkdir -p $(dir $@)
	$(CROSS_CC) $(CROSS_CFLAGS) -o $@ $(APP_$*_SRCS) $(KUI_SRCS) $(FBINK_A) -lm
	$(CROSS_TC)-strip $@

cross: $(FBINK_A) $(addprefix $(OUT)/,$(APPS))

build:
	[ -d third_party/FBInk ] || ./tools/fetch-deps.sh
	docker run --rm -v "$(CURDIR)":/src $(DOCKER_IMG) make cross

clean:
	rm -rf build dist

.PHONY: test cross build clean
