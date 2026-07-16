#
# TurboJS Javascript Engine
#
# Copyright (c) 2017-2026 Fabrice Bellard
# Copyright (c) 2017-2024 Charlie Gordon
# Copyright (c) 2023-2026 Ben Noordhuis
# Copyright (c) 2023-2026 Saúl Ibarra Corretgé
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

BUILD_DIR=build
BUILD_TYPE?=Release
INSTALL_PREFIX?=/usr/local

TURBOJS=$(BUILD_DIR)/turbojs
TURBOJSC=$(BUILD_DIR)/turbojsc
RUN262=$(BUILD_DIR)/run-test262

JOBS?=$(shell getconf _NPROCESSORS_ONLN)
ifeq ($(JOBS),)
JOBS := $(shell sysctl -n hw.ncpu)
endif
ifeq ($(JOBS),)
JOBS := $(shell nproc)
endif
ifeq ($(JOBS),)
JOBS := 4
endif

all: $(TURBOJS)

amalgam: TEMP := $(shell mktemp -d)
amalgam: $(TURBOJS)
	$(TURBOJS) tools/amalgamation/amalgamate.js $(TEMP)/turbojs-amalgam.c
	cp src/api/turbojs.h src/api/turbojs-libc.h $(TEMP)
	cd $(TEMP) && zip -9 turbojs-amalgam.zip turbojs-amalgam.c src/api/turbojs.h src/api/turbojs-libc.h
	cp $(TEMP)/turbojs-amalgam.zip $(BUILD_DIR)
	cd $(TEMP) && $(RM) turbojs-amalgam.zip turbojs-amalgam.c src/api/turbojs.h src/api/turbojs-libc.h
	$(RM) -d $(TEMP)

fuzz:
	clang -g -O1 -fsanitize=address,undefined,fuzzer -o fuzz tests/fuzz/parser_fuzz.c
	./fuzz

$(BUILD_DIR):
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX)

$(TURBOJS): $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j $(JOBS)

$(TURBOJSC): $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target turbojsc -j $(JOBS)

install: $(TURBOJS) $(TURBOJSC)
	cmake --build $(BUILD_DIR) --target install

clean:
	cmake --build $(BUILD_DIR) --target clean

codegen: $(TURBOJSC)
	$(TURBOJSC) -ss -o src/generated/runtime/repl.c -m repl.js
	$(TURBOJSC) -ss -o src/generated/runtime/standalone.c -m standalone.js
	$(TURBOJSC) -e -o src/generated/runtime/function_source.c tests/function_source.js
	$(TURBOJSC) -e -o gen/hello.c examples/hello.js
	$(TURBOJSC) -e -o gen/hello_module.c -m examples/hello_module.js
	$(TURBOJSC) -e -o gen/test_fib.c -m examples/test_fib.js
	$(TURBOJSC) -C -ss -o src/generated/builtins/array_fromasync.h src/builtins/sources/array_fromasync.js
	$(TURBOJSC) -C -ss -o src/generated/builtins/iterator_zip.h src/builtins/sources/iterator_zip.js
	$(TURBOJSC) -C -ss -o src/generated/builtins/iterator_zip_keyed.h src/builtins/sources/iterator_zip_keyed.js

debug:
	BUILD_TYPE=Debug $(MAKE)

distclean:
	@rm -rf $(BUILD_DIR)

stats: $(TURBOJS)
	$(TURBOJS) -qd

jscheck: CFLAGS=-I. -Isrc -Isrc/api -D_GNU_SOURCE -DJS_CHECK_JSVALUE -Wall -Werror -fsyntax-only
jscheck:
	$(CC) $(CFLAGS) tests/api/api_test.c
	$(CC) $(CFLAGS) tests/unit/c_api_test.c
	$(CC) $(CFLAGS) tests/fuzz/parser_fuzz.c
	$(CC) $(CFLAGS) src/generated/runtime/function_source.c
	$(CC) $(CFLAGS) gen/hello.c
	$(CC) $(CFLAGS) gen/hello_module.c
	$(CC) $(CFLAGS) src/generated/runtime/repl.c
	$(CC) $(CFLAGS) src/generated/runtime/standalone.c
	$(CC) $(CFLAGS) gen/test_fib.c
	$(CC) $(CFLAGS) apps/turbojs/main.c
	$(CC) $(CFLAGS) apps/turbojsc/main.c
	$(CC) $(CFLAGS) runtime/libc/turbojs-libc.c
	$(CC) $(CFLAGS) src/generated/turbojs_engine_unit.c src/core/version_api.c src/runtime/runtime_config_api.c src/runtime/context_access_api.c src/runtime/job_api.c src/runtime/lifecycle_api.c src/runtime/job_enqueue_api.c src/objects/class_proto_api.c src/gc/exception_state_api.c src/builtins/promise_hooks_api.c src/gc/memory_diagnostics_api.c src/modules/module_api.c
	$(CC) $(CFLAGS) apps/test262-runner/main.c

# effectively .PHONY because it doesn't generate output
ctest: CFLAGS=-std=c11 -fsyntax-only -Wall -Wextra -Werror -pedantic
ctest: tests/unit/c_api_test.c src/api/turbojs.h
	$(CC) $(CFLAGS) -DJS_NAN_BOXING=0 $<
	$(CC) $(CFLAGS) -DJS_NAN_BOXING=1 $<

# effectively .PHONY because it doesn't generate output
cxxtest: CXXFLAGS=-std=c++11 -fsyntax-only -Wall -Wextra -Werror -pedantic
cxxtest: tests/api/cpp_api_test.cc src/api/turbojs.h
	$(CXX) $(CXXFLAGS) -DJS_NAN_BOXING=0 $<
	$(CXX) $(CXXFLAGS) -DJS_NAN_BOXING=1 $<

test: $(TURBOJS)
	$(RUN262) -c tests/config/default.conf

test262: $(TURBOJS)
	$(RUN262) -m -c tests/config/test262.conf -a

test262-fast: $(TURBOJS)
	$(RUN262) -m -c tests/config/test262.conf -c tests/config/test262_fast.conf -a

test262-update: $(TURBOJS)
	$(RUN262) -u -c tests/config/test262.conf -a -t 1

test262-check: $(TURBOJS)
	$(RUN262) -m -c tests/config/test262.conf -E -a

microbench: $(TURBOJS)
	$(TURBOJS) tests/microbench.js

unicode_gen: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target unicode_gen

src/unicode/unicode_tables.h: unicode_gen
	$(BUILD_DIR)/unicode_gen unicode $@

.PHONY: all amalgam ctest cxxtest debug fuzz jscheck install clean codegen distclean stats test test262 test262-update test262-check microbench unicode_gen $(TURBOJS) $(TURBOJSC)
