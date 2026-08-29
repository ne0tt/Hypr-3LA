# Regenerates generated.js from the plugin sources. The viewer deliberately has
# no shader of its own -- see sync.py.
VIEWER_DEPS = sync.py ../3LA-GlitchClose/shader.hpp ../3LA-GlitchClose/main.cpp background-src.jpg

all: generated.js

generated.js: $(VIEWER_DEPS)
	python3 sync.py

# compile the extracted GLSL as ES 3.00 without a browser
check: generated.js
	glslangValidator .build/glitch.vert .build/glitch.frag && echo "shader OK"

open: generated.js
	./run.sh

# add to the app launcher (quickshell:toggleAppLauncher etc.)
install-desktop:
	install -Dm644 3LA-GlitchClose-Viewer.desktop \
		$(HOME)/.local/share/applications/3LA-GlitchClose-Viewer.desktop
	update-desktop-database $(HOME)/.local/share/applications 2>/dev/null || true
	@echo "installed; search for \"3LA-GlitchClose Tuner\""

clean:
	rm -rf generated.js .build

.PHONY: all check open clean install-desktop
