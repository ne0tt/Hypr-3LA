PLUGIN_NAME = 3LA-TitleBars
SOURCES     = main.cpp TitleBarDecoration.cpp
HEADERS     = TitleBarDecoration.hpp globals.hpp
CXX        ?= g++

# -g is omitted on purpose: it made the .so much larger with no codegen
# benefit. Use `make debug` when you actually need symbols.
# pango measures the title's cap band; the compositor already has it
# loaded, the explicit --libs just keeps the .so honest about what it calls.
CXXFLAGS += -shared -fPIC -fno-gnu-unique -std=c++26 -O2 \
            `pkg-config --cflags pixman-1 libdrm hyprland pangocairo`
LDLIBS   += `pkg-config --libs pangocairo`

all: $(PLUGIN_NAME).so

$(PLUGIN_NAME).so: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDLIBS)
	strip --strip-unneeded $@

debug: CXXFLAGS += -g
debug: clean
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(PLUGIN_NAME).so $(LDLIBS)

clean:
	rm -f $(PLUGIN_NAME).so

.PHONY: all clean debug
