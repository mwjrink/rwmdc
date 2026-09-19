[default]
list:
    just --list

alias b := build
alias r := run
alias br := build-run
alias brr := build-run-rune
alias g := gitcommitpush

# export ENABLE_HDR_WSI := "1"

export ASAN_OPTIONS := "abort_on_error=1:halt_on_error=1:fast_unwind_on_malloc=0"
export UBSAN_OPTIONS := "abort_on_error=1:halt_on_error=1"
export LSAN_OPTIONS := "report_objects=1:verbosity=1:suppressions=lsan.supp"

# TODO remove aftermath/enable it only in debug
# NOTE Never turn on fast math, it's just bad

# TODO -std=c99

# aftermath/lib/x64/libGFSDK_Aftermath_Lib.x64.so \
complibs := "/lib/libm.so                                    \
             /lib/libc.so                                    \
             /lib/libvulkan.so                               \
             /lib/libevdev.so"
complibs-rune := "/lib/libm.so               \
                  /lib/libc.so               \
                  /lib/libktx.so             \
                  /lib/libmeshoptimizer.so   \
                  /lib/libassimp.so          \
                  /lib/libwayland-client.so  \
                  /lib/libvulkan.so          \
                  /lib/libevdev.so"
compoptheavy := " -O3                           \
                  -g0                           \
                  -march=native                 \
                  -flto                         \
                  -fno-math-errno"
complogall := " -D LOG_LEVEL_VERBOSE            \
                -D LOG_LEVEL_INFO               \
                -D LOG_LEVEL_DEBUG              \
                -D LOG_LEVEL_WARNING            \
                -D LOG_LEVEL_ERROR              \
                -D LOG_LEVEL_CRITICAL"
complogdebug := " -D LOG_LEVEL_DEBUG            \
                  -D LOG_LEVEL_WARNING          \
                  -D LOG_LEVEL_ERROR            \
                  -D LOG_LEVEL_CRITICAL"
complogwarn := " -D LOG_LEVEL_WARNING           \
                 -D LOG_LEVEL_ERROR             \
                 -D LOG_LEVEL_CRITICAL"
complogdist := " -D LOG_LEVEL_ERROR             \
                 -D LOG_LEVEL_CRITICAL"

# TODO maybe use this? Weird cause VERBOSE(name) -> name is unused
# -Werror                           \
compwarn := " -Wall                             \
              -Wextra                           \
              -Wno-unused-parameter             \
              -Wno-unused-function              \
              -Wdouble-promotion                \
              -Wconversion                      \
              -Wno-sign-conversion              \
              -Wno-ignored-qualifiers           \
              --warning-suppression-mappings=warnings.supp"
compsanitize := " -fsanitize=address            \
                  -fsanitize-trap=undefined"
compdebug := " -D __DEBUG                       \
               -g3                              \
               -fno-omit-frame-pointer"

# fPIC is actually about small binary, not compile times

compfast := " -fPIC"
corebuild := " -I src                        \
               -I aftermath                  \
               -x c                          \
               -msse2"

# -fno-signed-zeros             \
#
# TODO ldd ritual
# TODO check this out:
# -Rpass-missed # INCREDIBLY interesting flag that tells you when and why some optimizations were missed
#
# -D LOG_LEVEL_VERBOSE          \
# -D LOG_LEVEL_INFO             \
# loglevel := if verbosity == "all" { logall }
#            else if verbosity == "a" { logall }
#            else if verbosity == "debug" { logdebug }
#            else if verbosity == "d" { logdebug }
#            else { logdist }

gitcommitpush msg:
    @git add .
    @git commit -m "{{ msg }}"
    @git push

check:
    #!/usr/bin/env bash
    set -euo pipefail
    header_count=$(ls src/lib/grim/gfx | wc -l)
    includes=$(rg '#include <lib/grim/gfx' src/lib/grim/gfx/internal_graphics.h | wc -l)
    if (( includes + 1 == header_count )); then
        echo "Check passed!"
        exit 0
    else
        echo "You are missing imports in internal_graphics.h"
        exit 1
    fi

check-deps:
    @ldd ritual

gen-xdg-shell:
    @wayland-scanner client-header </usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > lib/wayland/xdg-shell.h
    @wayland-scanner private-code </usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml > lib/wayland/xdg-shell.c

# /usr/share/wayland-protocols/unstable/relative-pointer/relative-pointer-unstable-v1.xml
gen-relative-pointer:
    @wayland-scanner client-header </usr/share/wayland-protocols/unstable/relative-pointer/relative-pointer-unstable-v1.xml > lib/wayland/relative-pointer.h
    @wayland-scanner private-code </usr/share/wayland-protocols/unstable/relative-pointer/relative-pointer-unstable-v1.xml > lib/wayland/relative-pointer.c

gen-pointer-constraints:
    @wayland-scanner client-header </usr/share/wayland-protocols/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml > lib/wayland/pointer-constraints.h
    @wayland-scanner private-code </usr/share/wayland-protocols/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml > lib/wayland/pointer-constraints.c

[arg("debug", long="debug", short="d", help="debug mode")]
[arg("warnings", long="warn", short="w", help="turn on warnings")]
[arg("sanitize", long="sanitize", short="s", help="address sanitizer")]
[arg("opt", long="optimizations", short="O", help="h for heavy\nanything else for none")]
[arg("source", long="sourcefile", short="i", help="source file to compile")]
[arg("verbosity", long="verbosity", short="v", help="a for all\nd for debug\nanything else for only error and critical")]
[arg("output", long="outputfilename", short="o", help="output filename")]
[arg("shader_print", long="shader-print", short="p", help="turn on shader printf")]
[arg("displayserverprotocol", long="displayserverprotocol", help="Choose between x11 and wayland")]
buildn verbosity="a" shader_print="on" warnings="on" debug="on" sanitize="on" opt="none" output="ritual" source="main.c" displayserverprotocol="wayland":
    @clang                                                                                                    \
        {{ if warnings == "on" { compwarn } else { "" } }}                                                    \
        {{ if debug == "on" { compdebug } else { "" } }}                                                      \
        {{ if sanitize == "on" { compsanitize } else { "" } }}                                                \
        {{ compfast }}                                                                                        \
        {{ complibs }}                                                                                        \
        {{ if displayserverprotocol == "x11" { "/lib/libX11.so" } else { "/lib/libwayland-client.so" } }}     \
        {{ if displayserverprotocol == "x11" { "-D X11" } else { "-D WAYLAND" } }}                            \
        {{ if verbosity == "all" { complogall } else if verbosity == "a" { complogall } else { "" } }}        \
        {{ if verbosity == "debug" { complogdebug } else if verbosity == "d" { complogdebug } else { "" } }}  \
        {{ if verbosity == "warn" { complogwarn } else if verbosity == "w" { complogwarn } else { "" } }}     \
        {{ if verbosity == "dist" { complogdist } else { "" } }}                                              \
        {{ if shader_print == "on" { "-D __SHADER_PRINT" } else { "" } }}                                     \
        {{ if opt == "h" { compoptheavy } else if opt == "heavy" { compoptheavy } else { "" } }}              \
        -o {{ output }}                                                                                       \
        {{ corebuild }}                                                                                       \
        src/{{ source }}

[arg("debug", long="debug", short="d", help="debug mode")]
[arg("warnings", long="warn", short="w", help="turn on warnings")]
[arg("sanitize", long="sanitize", short="s", help="address sanitizer")]
[arg("opt", long="optimizations", short="O", help="h for heavy\nanything else for none")]
[arg("source", long="sourcefile", short="i", help="source file to compile")]
[arg("verbosity", long="verbosity", short="v", help="a for all\nd for debug\nanything else for only error and critical")]
[arg("output", long="outputfilename", short="o", help="output filename")]
[arg("shader_print", long="shader-print", short="p", help="turn on shader printf")]
buildn-rune verbosity="a" shader_print="on" warnings="on" debug="on" sanitize="on" opt="none" output="rune" source="rune.c":
    @clang                                                                                                    \
        {{ if warnings == "on" { compwarn } else { "" } }}                                                    \
        {{ if debug == "on" { compdebug } else { "" } }}                                                      \
        {{ if sanitize == "on" { compsanitize } else { "" } }}                                                \
        {{ compfast }}                                                                                        \
        {{ complibs-rune }}                                                                                        \
        {{ if verbosity == "all" { complogall } else if verbosity == "a" { complogall } else { "" } }}        \
        {{ if verbosity == "debug" { complogdebug } else if verbosity == "d" { complogdebug } else { "" } }}  \
        {{ if verbosity == "warn" { complogwarn } else if verbosity == "w" { complogwarn } else { "" } }}     \
        {{ if verbosity == "dist" { complogdist } else { "" } }}                                              \
        {{ if shader_print == "on" { "-D __SHADER_PRINT" } else { "" } }}                                     \
        {{ if opt == "h" { compoptheavy } else if opt == "heavy" { compoptheavy } else { "" } }}              \
        -o {{ output }}                                                                                       \
        {{ corebuild }}                                                                                       \
        src/{{ source }}

build-rune:
    @just buildn-rune -v=d -s=on -d=on -p=off -w=on

build:
    @just buildn -v=d -s=on -d=on -p=off -w=on

build-nosan:
    @just buildn -v=d -s=off -d=on -p=off -w=on

build-shader-print:
    @just buildn -v=a -s=on -d=on -p=on -w=on

build-release:
    @just buildn -v=w -s=on -d=off -p=off -w=on -O=h

build-dist:
    @just buildn -v=dist -s=off -d=off -p=off -w=off -O=h

run:
    @./ritual

run-rune:
    @./rune

debug:
    @gdb ./ritual

compile-shaders:
    slangc assets/shaders/simple.slang -entry vertexMain -stage vertex -matrix-layout-column-major -o assets/shaders/simple.vert.spv -target spirv
    slangc assets/shaders/simple.slang -entry fragmentMain -stage fragment -matrix-layout-column-major -o assets/shaders/simple.frag.spv -target spirv
    slangc assets/shaders/meshcull.comp.slang -entry meshCullMain -stage compute -matrix-layout-column-major -o assets/shaders/meshcull.comp.spv -target spirv
    slangc assets/shaders/simple.meshtask.slang -entry taskMain -stage task -matrix-layout-column-major -o assets/shaders/simple.task.spv -target spirv
    slangc assets/shaders/simple.meshtask.slang -entry meshMain -stage mesh -matrix-layout-column-major -o assets/shaders/simple.mesh.spv -target spirv

transpile-shaders:
    slangc assets/shaders/simple.slang -entry vertexMain -stage vertex -matrix-layout-column-major -o assets/shaders/simple.vert.glsl -target glsl
    slangc assets/shaders/simple.slang -entry fragmentMain -stage fragment -matrix-layout-column-major -o assets/shaders/simple.frag.glsl -target glsl
    slangc assets/shaders/meshcull.comp.slang -entry meshCullMain -stage compute -matrix-layout-column-major -o assets/shaders/meshcull.comp.glsl -target glsl
    slangc assets/shaders/simple.meshtask.slang -entry taskMain -stage task -matrix-layout-column-major -o assets/shaders/simple.task.glsl -target glsl
    slangc assets/shaders/simple.meshtask.slang -entry meshMain -stage mesh -matrix-layout-column-major -o assets/shaders/simple.mesh.glsl -target glsl

# use glslc, allows #include and just wraps glslang
# --target-env vulkan1.4 \ implies: spirv1.6
# --scalar-block-layout \
# --nan-clamp \ # favor non nan in min, max etc
# -t \ # multithread
# for dist/run do -g0 instead of -g

# --sep meshCullMain \ # rename meshCullMain to the -e option, main
_compile-shaders-glsl name stage entry="main":
    @glslang \
        assets/shaders/{{ name }}.{{ stage }}.glsl \
        -o assets/shaders/{{ name }}.{{ stage }}.spv \
        -S {{ stage }} \
        --target-env vulkan1.4 \
        --validate-io \
        -lto \
        --spirv-val \
        -g \
        -e {{ entry }} \
        assets/shaders/glslangValidator.conf

compile-shaders-glsl:
    @just _compile-shaders-glsl simple frag
    @just _compile-shaders-glsl simple task
    @just _compile-shaders-glsl simple mesh
    @just _compile-shaders-glsl meshcull comp

build-run:
    @just check
    # @just compile-shaders
    @just compile-shaders-glsl
    @just build
    @just run

build-run-rune:
    @just build-rune
    @just run-rune
