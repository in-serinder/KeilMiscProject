# Makefile - Linux build for capsaenc (source-only delivery).
# The Windows build (MSVC freestanding) uses build.bat; this one links against
# the regular glibc and produces build/capsaenc and build/stub.
#
#   make            - build both tools + size check
#   make selftest   - run the built-in algorithm self test
#   make e2e        - pack, extract and verify a round trip
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -s -Wall -Wextra -D_DEFAULT_SOURCE
PY       ?= python3

CORE := src/common.cpp src/sha256.cpp src/aes.cpp src/lz4.cpp src/payload.cpp src/io_posix.cpp

.PHONY: all selftest e2e clean

all: build/capsaenc size-check

build/stub: src/stub.cpp $(CORE) | build
	$(CXX) $(CXXFLAGS) -o $@ src/stub.cpp $(CORE)

build/stub_blob.h: build/stub tools/embed.py
	$(PY) tools/embed.py build/stub src/stub_blob.h --name g_stub

build/capsaenc: src/main.cpp $(CORE) build/stub_blob.h src/src_blob.h | build
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp $(CORE)

src/src_blob.h: tools/embed_src.py $(filter-out src/stub_blob.h,$(wildcard src/*.h) $(wildcard src/*.cpp) src/chkstk.asm)
	$(PY) tools/embed_src.py src src/src_blob.h

size-check: build/capsaenc build/stub
	$(PY) tools/checksize.py build/capsaenc build/stub --limit 120K

selftest: build/capsaenc
	./build/capsaenc --selftest

e2e: build/capsaenc
	@rm -rf build/e2e && mkdir -p build/e2e
	@printf 'hello capsaenc\nthis is the e2e payload.\n' > build/e2e/note.txt
	@printf 'hello capsaenc\n' | ./build/capsaenc -f build/e2e/note.txt -o build/e2e/pack --pass-stdin
	@printf 'hello capsaenc\n' | ./build/e2e/pack -o build/e2e --no-save > build/e2e/printed.txt
	@cmp build/e2e/note.txt build/e2e/printed.txt
	@echo "e2e OK"

build:
	mkdir -p build

clean:
	rm -rf build
