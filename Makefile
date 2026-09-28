# Galley — a review loop for books written in Markdown.
#
# Plain make rather than CMake: the app is a single binary with four
# dependencies, all of which pkg-config already knows about. A CMakeLists.txt
# can arrive alongside the PKGBUILD when there is something to package.

QT_MODULES := Qt6Widgets Qt6WebEngineWidgets Qt6WebChannel
PKGS       := $(QT_MODULES) md4c

QT_BINS := $(shell pkg-config --variable=host_bins Qt6Core 2>/dev/null)
MOC     := $(if $(wildcard $(QT_BINS)/moc),$(QT_BINS)/moc,/usr/lib/qt6/moc)
RCC     := $(if $(wildcard $(QT_BINS)/rcc),$(QT_BINS)/rcc,/usr/lib/qt6/rcc)

# A v-prefixed tag is the git convention; a v-prefixed version string is not.
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null | sed 's/^v//' || echo 0.1.0)
PREFIX  ?= /usr
DESTDIR ?=

CXX      ?= g++
CXXFLAGS += -std=c++20 -fPIC -O2 -Wall -Wextra -Isrc -DGALLEY_VERSION='"$(VERSION)"' \
            $(shell pkg-config --cflags $(PKGS))
LIBS     := $(shell pkg-config --libs $(PKGS))

BUILD := build
TARGET := galley
PROFILE_TEST := $(BUILD)/agent-profiles-test
MODEL_TEST := $(BUILD)/model-edges-test

SRCS := $(wildcard src/*.cpp)
OBJS := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRCS))

# Headers declaring Q_OBJECT need moc. Discovered rather than listed so a new
# QObject subclass does not silently fail to link.
MOC_HDRS := $(shell grep -l Q_OBJECT src/*.h 2>/dev/null)
MOC_SRCS := $(patsubst src/%.h,$(BUILD)/moc_%.cpp,$(MOC_HDRS))
MOC_OBJS := $(patsubst $(BUILD)/moc_%.cpp,$(BUILD)/moc_%.o,$(MOC_SRCS))

QRC      := web/web.qrc
QRC_SRC  := $(BUILD)/qrc_web.cpp
QRC_OBJ  := $(BUILD)/qrc_web.o
QRC_DEPS := $(shell sed -n 's:.*<file>\(.*\)</file>.*:web/\1:p' $(QRC) 2>/dev/null)

.PHONY: all clean run install uninstall check
all: $(TARGET)

$(TARGET): $(OBJS) $(MOC_OBJS) $(QRC_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/moc_%.cpp: src/%.h | $(BUILD)
	$(MOC) $< -o $@

$(BUILD)/moc_%.o: $(BUILD)/moc_%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(QRC_SRC): $(QRC) $(QRC_DEPS) | $(BUILD)
	$(RCC) --name web $< -o $@

$(QRC_OBJ): $(QRC_SRC)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

$(PROFILE_TEST): test/agent_profiles.cpp $(BUILD)/AgentProfiles.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(shell pkg-config --libs Qt6Core)

$(MODEL_TEST): test/model_edges.cpp $(BUILD)/AgentProfiles.o $(BUILD)/ModelDiscovery.o $(BUILD)/moc_ModelDiscovery.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(shell pkg-config --libs Qt6Qml)

# The regression test: md4c's parse and BlockScanner's independent scan of the
# same bytes, reconciled over test/book. UPDATE=1 rewrites the expected output.
check: $(TARGET) $(PROFILE_TEST) $(MODEL_TEST)
	@test/run.sh

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/galley
	install -Dm644 packaging/galley.desktop \
		$(DESTDIR)$(PREFIX)/share/applications/galley.desktop
	install -Dm644 packaging/galley.svg \
		$(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/galley.svg
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/galley/README.md
	install -Dm644 doc/ROADMAP.md $(DESTDIR)$(PREFIX)/share/doc/galley/ROADMAP.md
	install -Dm644 doc/ARCHITECTURE.md $(DESTDIR)$(PREFIX)/share/doc/galley/ARCHITECTURE.md
	install -Dm644 doc/ROUNDS-AND-GIT.md $(DESTDIR)$(PREFIX)/share/doc/galley/ROUNDS-AND-GIT.md

# Mirrors install exactly. Leaves ~/.config/galley and every book's .galley
# alone: those are the author's, not the package's.
uninstall:
	rm -f  $(DESTDIR)$(PREFIX)/bin/galley
	rm -f  $(DESTDIR)$(PREFIX)/share/applications/galley.desktop
	rm -f  $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/galley.svg
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/galley

clean:
	rm -rf $(BUILD) $(TARGET)

run: $(TARGET)
	./$(TARGET) $(ARGS)

-include $(wildcard $(BUILD)/*.d)
