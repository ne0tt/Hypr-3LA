PLUGIN_NAME = 3LA-Feed-Loss
SOURCES     = main.cpp FeedLossEffect.cpp
HEADERS     = FeedLossEffect.hpp globals.hpp
CXX        ?= g++

# -g is omitted on purpose (see 3LA-Corners/Makefile). Use `make debug` for symbols.
CXXFLAGS += -shared -fPIC -fno-gnu-unique -std=c++26 -O2 \
            `pkg-config --cflags pixman-1 libdrm hyprland`

all: $(PLUGIN_NAME).so

$(PLUGIN_NAME).so: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@
	strip --strip-unneeded $@

debug: CXXFLAGS += -g
debug: clean
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(PLUGIN_NAME).so

clean:
	rm -f $(PLUGIN_NAME).so

.PHONY: all clean debug
