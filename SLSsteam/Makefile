#Thanks to https://stackoverflow.com/questions/52034997/how-to-make-makefile-recompile-when-a-header-file-is-changed for the -MMD & -MP flags
#Without them headers wouldn't trigger recompilation

#Bare `make` (used by docker/build.sh) must build SLSsteam.so + helpers,
#not whichever rule happens to appear first (previously bin/accela-helper)
.DEFAULT_GOAL := audit-libs

#Force g++ cause clang crashes on some hooks
CXX := g++

libs := $(wildcard lib/*.a)
srcs := $(shell find src/ -type f -iname "*.cpp")

CXXFLAGS := -O3 -flto=auto -fPIC -m32 -std=c++20 -Wall -Wextra -Wpedantic -Wno-error=format-security -D_GLIBCXX_USE_CXX11_ABI=0
CXXFLAGS += -floop-block -fgraphite-identity -floop-parallelize-all -pipe -fopenmp -fomit-frame-pointer

LDFLAGS := -shared -Wl,--no-undefined
LDFLAGS += $(shell pkg-config --libs "openssl")
LDFLAGS += $(shell pkg-config --libs "libcurl")

JOBS := $(shell nproc)

#DATE := $(shell date "+%Y%m%d%H%M%S")
DATE := $(shell grep '^Version:' res/version | cut -d' ' -f2 | xargs)

ifeq ($(TRACE),1)
	CXXFLAGS += -D "TRACE"
endif

ifeq ($(DEBUG),1)
	CXXFLAGS += -D "DEBUG" -g -O0
	CXXFLAGS := $(filter-out -O3,$(CXXFLAGS))
	CXXFLAGS := $(filter-out -flto=auto,$(CXXFLAGS))
else
	CXXFLAGS += -Wno-unused-parameter -Wno-unused-variable
endif

ifeq ($(NATIVE),1)
	CXXFLAGS += -march=native
endif

#Speed up compilation if additional dependencies are found
ifeq ($(shell type ccache &> /dev/null && echo "found"),found)
	export PATH := /usr/lib/ccache/bin:$(PATH)
endif
ifeq ($(shell type mold &> /dev/null && echo "found"),found)
	LDFLAGS += -fuse-ld=mold
else ifeq ($(shell type ld.gold &> /dev/null && echo "found"),found)
	LDFLAGS += -fuse-ld=gold
endif

COMPILER_VERSION := $(shell $(CXX) -dumpfullversion -dumpversion)
FLAGSSHA := $(shell echo "$(CXX) $(COMPILER_VERSION) & $(CXXFLAGS) & $(LDFLAGS)" | sha256sum | cut -d " " -f 1)
SLSSTEAMSO := bin/SLSsteam-$(FLAGSSHA).so
objs := $(srcs:src/%.cpp=obj/$(FLAGSSHA)/%.o)
deps := $(objs:%.o=%.d)

# accela-helper: standalone binary exec()'d after fork() to avoid
# multithreading deadlocks. Must be built as a 32-bit executable to
# match the SLSsteam.so library it links against.
HELPER_SRCS := \
	src/CppAccela/accela-helper.cpp \
	src/CppAccela/accelapath.cpp \
	src/CppAccela/accelamanifest.cpp \
	src/CppAccela/accelaluaparser.cpp \
	src/CppAccela/acceladepotdownloader.cpp \
	src/CppAccela/accelapostprocess.cpp \
	src/curl.cpp

HELPER_OBJS := $(patsubst src/%.cpp,obj/$(FLAGSSHA)/%.o,$(HELPER_SRCS))
HELPER_DEPS := $(HELPER_OBJS:%.o=%.d)
HELPER_LDFLAGS := $(shell pkg-config --libs "openssl")
HELPER_LDFLAGS += $(shell pkg-config --libs "libcurl")

bin/accela-helper: $(HELPER_OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $^ -o $@ $(HELPER_LDFLAGS)
	chmod 755 $@

-include $(HELPER_DEPS)

audit-libs:
	$(MAKE) -j $(JOBS) $(SLSSTEAMSO) bin/library-inject.so bin/accela-helper
	$(MAKE) link-bins

tools:
	$(MAKE) -j 2 schema-grabber ticket-grabber

link-bins:
	-test -f "$(SLSSTEAMSO)" && ln -f "$(SLSSTEAMSO)" "bin/SLSsteam.so"
	-test -f "tools/library-inject/library-inject.so" && ln -f "tools/library-inject/library-inject.so" "bin/library-inject.so"

$(SLSSTEAMSO): $(objs) $(libs)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $^ -o $(SLSSTEAMSO) $(LDFLAGS)
	$(MAKE) link-bins

bin/library-inject.so:
	@mkdir -p bin
	$(MAKE) -C tools/library-inject
	$(MAKE) link-bins

schema-grabber:
	$(MAKE) -C tools/schema-grabber

ticket-grabber:
	$(MAKE) -C tools/ticket-grabber

-include $(deps)
obj/$(FLAGSSHA)/update.o: src/update.cpp res/version
	$(shell ./embed-version.sh)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isysteminclude -MMD -MP -c $< -o $@

obj/$(FLAGSSHA)/config.o: src/config.cpp res/config.yaml
	$(shell ./embed-config.sh)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isysteminclude -MMD -MP -c $< -o $@

obj/$(FLAGSSHA)/%.o : src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -isysteminclude -MMD -MP -c $< -o $@

clean-libs:
	rm -rvf \
		"obj/" \
		"bin/" \
		"zips/" \
		"tools/ticket-grabber/bin" \
		"tools/ticket-grabber/obj" \
		"tools/schema-grabber/bin" \
		"tools/schema-grabber/obj"

clean-tools:
	-$(MAKE) -C tools/schema-grabber clean
	-$(MAKE) -C tools/ticket-grabber clean

install:
	sh setup.sh install

uninstall:
	sh setup.sh uninstall

zips: build
	@mkdir -p zips
	7z a -mx9 -m9=lzma2 \
		"zips/SLSsteam $(DATE).7z" \
		"bin/SLSsteam.so" \
		"bin/library-inject.so" \
		"setup.sh" \
		"docs/LICENSE" \
		"res/config.yaml" \
		"tools/ticket-grabber/bin/Release/net9.0/linux-x64/publish/ticket-grabber" \
		"tools/schema-grabber/bin/Release/net9.0/linux-x64/publish/schema-grabber"

	#Compatibility for Github issues
	7z a -mx9 -m9=lzma \
		"zips/SLSsteam $(DATE).zip" \
		"bin/SLSsteam.so" \
		"bin/library-inject.so" \
		"setup.sh" \
		"docs/LICENSE" \
		"res/config.yaml" \
		"tools/ticket-grabber/bin/Release/net9.0/linux-x64/publish/ticket-grabber" \
		"tools/schema-grabber/bin/Release/net9.0/linux-x64/publish/schema-grabber"

zips-config:
	7z a -mx9 -m9=lzma "zips/SLSsteam - SLSConfig $(DATE).zip" "$(HOME)/.config/SLSsteam/config.yaml"
	#Compatibility for Github issues
	7z a -mx9 -m9=lzma2 "zips/SLSsteam - SLSConfig $(DATE).7z" "$(HOME)/.config/SLSsteam/config.yaml"


clean: clean-libs clean-tools
build: audit-libs tools
rebuild: clean build
release: rebuild zips

.PHONY: \
	audit-libs \
	tools \
	link-bins \
	schema-grabber \
	ticket-grabber \
	clean-libs \
	clean-tools \
	install \
	uninstall \
	zips \
	zips-config \
	clean \
	build \
	rebuild \
	release
