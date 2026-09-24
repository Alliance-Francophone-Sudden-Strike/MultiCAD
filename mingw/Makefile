# MinGW-w64 cross build of MultiCAD on Linux.
#
#   make            build bin/cadMulti_mt.dll and bin/cadMulti.dll
#   make mt         only the fully-static cadMulti_mt.dll
#   make test       host unit tests, then LoadLibrary smoke-test both DLLs under Wine
#   make clean      remove obj/ and bin/
#
# The Visual Studio project stays the source of truth; this path copies src/
# into obj/gen/ and patches a few MSVC-isms (see prep.py). src/ is untouched.
# Outputs land in bin/ and obj/ - both already covered by .gitignore.

CXX     := i686-w64-mingw32-g++
HOST_CXX ?= c++
WINDRES := i686-w64-mingw32-windres
STRIP   := i686-w64-mingw32-strip

HERE    := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SRCDIR  := $(HERE)../src
GEN     := obj/gen

# -Os not -O2: GCC -O2 inlining costs ~28% .text here for no measured gain.
# Never add -fdata-sections: g_rendererState is a 48MB .bss and ld would
# emit it as 48MB of zeros on disk.
CXXFLAGS := -std=gnu++2b -m32 -Os -pipe -fno-strict-aliasing -fpermissive -w \
           -include $(HERE)msvc_compat.h \
           -I $(HERE)shim \
           -I $(GEN)/core -I $(GEN)/ui -I $(GEN)/patcher -I $(GEN)/profiles -I $(GEN)/stats -I $(GEN) \
           -DWIN32 -DNDEBUG -DMULTICAD_EXPORTS -D_WINDOWS -D_USRDLL \
           -DUNICODE -D_UNICODE \
           -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -DNTDDI_VERSION=0x06010000

# winhttp/bcrypt: StatsReporter.cpp names them with #pragma comment(lib), which
# GCC ignores.
LDLIBS  := -ldxguid -lddraw -lwinmm -lole32 -loleaut32 -luuid \
           -lgdi32 -luser32 -ladvapi32 -lshell32 -lshlwapi -lversion \
           -lwinhttp -lbcrypt
LDFLAGS := -m32 -shared -Wl,--subsystem,windows -Wl,--enable-stdcall-fixup \
           -Wl,--exclude-all-symbols $(HERE)cadMulti.def

SRCS := main.cpp mingw_export.cpp \
        core/pch.cpp core/cad.cpp core/renderer.cpp core/ScreenConfig.cpp core/util.cpp \
        patcher/DllMonitor.cpp patcher/DllVersionDetector.cpp patcher/GameDllHooks.cpp \
        patcher/MenuDllHooks.cpp patcher/PatchInstallers.cpp \
        stats/MatchStatsReader.cpp stats/OutcomeHook.cpp stats/StatsReporter.cpp \
        ui/UIFilter.cpp ui/ResolutionVerifier.cpp ui/ResolutionDialog.cpp
OBJS := $(addprefix obj/,$(SRCS:.cpp=.o))
RES  := obj/version.res

RT_DLLS := libstdc++-6.dll libgcc_s_dw2-1.dll libwinpthread-1.dll
GCC_LIBDIR := $(dir $(shell $(CXX) -print-libgcc-file-name))

SRC_FILES := $(shell find $(SRCDIR) -type f 2>/dev/null)

.PHONY: all mt clean test zoom-test group-panel-test zeppelin-panel-test world-isolation-test zoom-render-test analyze-dll-test prep
all: bin/cadMulti_mt.dll bin/cadMulti.dll
mt:  bin/cadMulti_mt.dll

prep: ; @python3 $(HERE)prep.py

# prep.py self-gates on a content signature and writes the stamp itself.
obj/.prep-stamp: $(SRC_FILES) $(HERE)prep.py $(HERE)mingw_export.cpp
	@python3 $(HERE)prep.py

obj/%.o: obj/.prep-stamp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $(GEN)/$*.cpp -o $@

$(RES): $(HERE)MultiCAD_mingw.rc obj/.prep-stamp
	@mkdir -p obj
	$(WINDRES) -O coff -I $(GEN)/core -I $(GEN) $< $@

bin/cadMulti_mt.dll: $(OBJS) $(RES)
	@mkdir -p bin
	$(CXX) $(LDFLAGS) -static -static-libgcc -static-libstdc++ \
	    -o $@ $(OBJS) $(RES) -Wl,-Bstatic -lstdc++ -lpthread $(LDLIBS)
	$(STRIP) --strip-unneeded $@
	@echo "  -> $@ (standalone)"

bin/cadMulti.dll: $(OBJS) $(RES)
	@mkdir -p bin
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(RES) $(LDLIBS)
	$(STRIP) --strip-unneeded $@
	@for d in $(RT_DLLS); do \
	    f=$$($(CXX) -print-file-name=$$d); \
	    [ -f "$$f" ] || f=$(GCC_LIBDIR)$$d; \
	    [ -f "$$f" ] || f=$$(find /usr -name $$d -path '*i686*' 2>/dev/null | head -1); \
	    cp "$$f" bin/ && $(STRIP) --strip-unneeded bin/$$d && echo "  -> bin/$$d"; \
	done
	@echo "  -> $@ (needs the 3 runtime DLLs above beside it)"

# LoadLibrary smoke test under Wine. Runs from bin/ so cadMulti.dll finds its
# sidecar runtime DLLs (mirrors dropping everything into the game folder).
test: zoom-test group-panel-test zeppelin-panel-test world-isolation-test all analyze-dll-test
	@i686-w64-mingw32-gcc -m32 -O2 $(HERE)loadtest.c -o bin/loadtest.exe
	@cd bin && for d in cadMulti_mt.dll cadMulti.dll; do \
	    echo "== $$d =="; WINEDEBUG=-all wine loadtest.exe "$$d" || exit 1; \
	 done
	@rm -f bin/loadtest.exe

# AnalyzeDll fingerprints game DLLs by hashing their .text section; the Python
# side re-derives the same hash independently. Feeds it real PEs plus a
# truncated and a non-PE file to cover the reject paths.
bin/analyze_dll_test.exe: ../tests/analyze_dll_test.cpp obj/patcher/DllVersionDetector.o obj/core/util.o
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -fno-access-control $< $(GEN)/patcher/DllVersionDetector.cpp \
	    $(GEN)/core/util.cpp -o $@ -static -static-libgcc -static-libstdc++ \
	    -Wl,-Bstatic -lstdc++ -lpthread $(LDLIBS) -lcrypt32

analyze-dll-test: bin/analyze_dll_test.exe bin/cadMulti.dll
	@head -c 300 bin/cadMulti.dll > obj/truncated.bin; echo "not a pe" > obj/garbage.bin
	@python3 ../tests/analyze_dll_test.py bin/analyze_dll_test.exe \
	    bin/cadMulti.dll bin/cadMulti_mt.dll bin/libstdc++-6.dll \
	    obj/truncated.bin obj/garbage.bin

zoom-test:
	@mkdir -p obj
	$(HOST_CXX) -std=c++20 -Wall -Wextra -pedantic -I$(SRCDIR)/core ../tests/zoom_test.cpp -o obj/zoom_test
	obj/zoom_test

group-panel-test:
	@mkdir -p obj
	$(HOST_CXX) -std=c++20 -Wall -Wextra -pedantic -I$(SRCDIR)/core -I$(SRCDIR)/patcher ../tests/group_panel_test.cpp -o obj/group_panel_test
	obj/group_panel_test

zeppelin-panel-test:
	@mkdir -p obj
	$(HOST_CXX) -std=c++20 -Wall -Wextra -pedantic -I$(SRCDIR)/core -I$(SRCDIR)/patcher ../tests/zeppelin_panel_test.cpp -o obj/zeppelin_panel_test
	obj/zeppelin_panel_test

world-isolation-test:
	@mkdir -p obj
	$(HOST_CXX) -std=c++20 -Wall -Wextra -pedantic -I$(SRCDIR)/core -I$(SRCDIR)/patcher ../tests/world_isolation_test.cpp -o obj/world_isolation_test
	obj/world_isolation_test

# Exercise the actual composition hook and sprite renderer under Wine.
bin/zoom_render_test.exe: ../tests/zoom_render_test.cpp $(OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -UNDEBUG -fno-access-control -static -static-libgcc -static-libstdc++ \
	    $< $(OBJS) -o $@ -Wl,-Bstatic -lstdc++ -lpthread $(LDLIBS)

zoom-render-test: bin/zoom_render_test.exe
	WINEDEBUG=-all wine $<

clean:
	rm -rf obj bin
