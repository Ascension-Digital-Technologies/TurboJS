BUILD_DIR ?= build
BUILD_TYPE ?= Release
INSTALL_PREFIX ?= /usr/local
JOBS ?=

CMAKE_CONFIGURE = cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX)
CMAKE_BUILD = cmake --build $(BUILD_DIR) $(if $(JOBS),--parallel $(JOBS),)

.PHONY: all configure build test install clean distclean architecture codegen engine-codegen amalgam

all: build

configure:
	$(CMAKE_CONFIGURE)

build:
	@test -f "$(BUILD_DIR)/CMakeCache.txt" || $(CMAKE_CONFIGURE)
	$(CMAKE_BUILD)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

install: build
	cmake --install $(BUILD_DIR)

architecture:
	python3 tools/validation/check_architecture.py

engine-codegen:
	python3 tools/generators/generate_engine_unit.py

codegen: build
	python3 tools/generators/generate_runtime.py --compiler $(BUILD_DIR)/turbojsc

amalgam: build
	@set -eu; \
	tmp=$$(mktemp -d); \
	trap 'rm -rf "$$tmp"' EXIT; \
	mkdir -p "$$tmp/include/turbojs"; \
	$(BUILD_DIR)/turbojs tools/amalgamation/amalgamate.js "$$tmp/turbojs-amalgam.c"; \
	cp include/turbojs/turbojs.h include/turbojs/turbojs-libc.h "$$tmp/include/turbojs/"; \
	(cd "$$tmp" && zip -9 turbojs-amalgam.zip turbojs-amalgam.c include/turbojs/turbojs.h include/turbojs/turbojs-libc.h); \
	cp "$$tmp/turbojs-amalgam.zip" "$(BUILD_DIR)/turbojs-amalgam.zip"

clean:
	@if test -f "$(BUILD_DIR)/CMakeCache.txt"; then cmake --build $(BUILD_DIR) --target clean; fi

distclean:
	rm -rf $(BUILD_DIR)
