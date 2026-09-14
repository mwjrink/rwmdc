[default]
list:
    @just --list

warnings := "-Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers"
text_cflags := `pkg-config --cflags libutf8proc xkbcommon`
text_libs := `pkg-config --libs libutf8proc xkbcommon`

# Optimized Slug executable. Both shaders are rebuilt from source.
build: compile-shaders
    @just _compile release wayland rwmd

# Vulkan validation and source-level debugging, without sanitizers.
build-debug: compile-shaders
    @just _compile debug wayland rwmd-debug

# Memory/undefined-behavior checks; not a performance build.
build-sanitize: compile-shaders
    @just _compile sanitize wayland rwmd-sanitize

# Same renderer using an X11 surface.
build-x11: compile-shaders
    @just _compile release x11 rwmd-x11

_compile mode protocol output:
    @clang -std=gnu17 -Isrc {{warnings}} {{text_cflags}} \
        {{if mode == "release" { "-O3 -march=native -flto -fno-math-errno" } else { "-O1 -g3 -fno-omit-frame-pointer -D__DEBUG" }}} \
        {{if mode == "sanitize" { "-fsanitize=address,undefined" } else { "" }}} \
        -DLOG_LEVEL_ERROR -DLOG_LEVEL_CRITICAL \
        {{if mode != "release" { "-DLOG_LEVEL_WARNING" } else { "" }}} \
        {{if protocol == "x11" { "-DX11" } else { "-DWAYLAND" }}} \
        src/main.c {{if protocol == "x11" { "" } else { "src/lib/wayland/xdg-shell.c" }}} \
        -o {{output}} -lm -lvulkan {{text_libs}} \
        {{if protocol == "x11" { "-lX11" } else { "-lwayland-client" }}}

# Run an existing build without including compilation in the launch workflow.
run *args:
    @./rwmd {{args}}

# Build and run the Slug benchmark. Pass --help for workload controls.
benchmark *args: build
    @./rwmd --benchmark {{args}}


# Deterministic CPU regressions; no display or Vulkan device is opened.
check:
    @just _test config_test
    @just _test kb_profile_test
    @just _test kb_context_test
    @just _test document
    @just _test parser_test
    @just _test layout_test
    @just _test editor_test

_test name:
    @clang -std=gnu17 -O1 -g -fsanitize=address,undefined -Isrc -DWAYLAND \
        {{warnings}} {{text_cflags}} tests/{{name}}.c src/lib/wayland/xdg-shell.c \
        -o /tmp/rwmd-{{name}}-test -lm -lvulkan -lwayland-client {{text_libs}}
    @/tmp/rwmd-{{name}}-test
compile-shaders:
    @just _shader vert
    @just _shader frag

_shader stage:
    @env -u LD_LIBRARY_PATH glslang -V --target-env vulkan1.3 -Os --spirv-val \
        -S {{stage}} assets/shaders/text_slug.{{stage}}.glsl \
        -o assets/shaders/text_slug.{{stage}}.spv

# Generated protocol sources are checked in; regenerate only when needed.
gen-xdg-shell:
    @wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml src/lib/wayland/xdg-shell.h
    @wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml src/lib/wayland/xdg-shell.c

# Isolated CPU shaping comparisons; HarfBuzz is required only by these recipes.
shaping-bench isa="avx2" *args:
    @bash bench/shaping-build.sh bench {{quote(isa)}} {{args}}

# Observer timings are diagnostic; use shaping-bench for production measurements.
shaping-profile isa="avx2" *args:
    @bash bench/shaping-build.sh profile {{quote(isa)}} {{args}}


# RDTSCP stage/lookup scopes only; micro-helper hooks compile out.
shaping-core isa="avx2" *args:
    @bash bench/shaping-build.sh core {{quote(isa)}} {{args}}
# Exact owned/upstream equality; HarfBuzz differences remain diagnostic.
shaping-verify isa="avx2" *args:
    @bash bench/shaping-build.sh verify {{quote(isa)}} {{args}}

# Deterministic multi-font/script differential diagnostics and JSONL report.
shaping-stress isa="avx2" *args:
    @bash bench/shaping-build.sh stress {{quote(isa)}} {{args}}

# Canonical KB-only language/font/length matrix, machine-readable results.
shaping-suite command="list" *args:
    @python3 bench/shaping-suite.py {{quote(command)}} {{args}}
