#!/usr/bin/env python3
"""Generate compile_commands.json for a single-TU C project from its justfile.

clangd only understands per-file argv (compile_commands.json) or one flat list
(compile_flags.txt). This project keeps its flags in justfile variables, so
parse those and emit the database instead of maintaining a third copy.

Usage:
    tools/gen_compile_db.py [--compiler clang|gcc] [--profile build|release|dist]
                            [--source src/main.c] [--out compile_commands.json]

The profile names match the justfile recipes (build, build-release, build-dist).
"""

import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# justfile recipe name -> argument set for `just buildn`. Mirrors the
# shortcuts at the bottom of the justfile; keep them in sync.
PROFILES = {
    "build": dict(verbosity="d", sanitize="on", debug="on", shader_print="off", warnings="on", opt="none"),
    "build-release": dict(verbosity="w", sanitize="on", debug="off", shader_print="off", warnings="on", opt="h"),
    "build-dist": dict(verbosity="dist", sanitize="off", debug="off", shader_print="off", warnings="off", opt="h"),
    "build-nosan": dict(verbosity="d", sanitize="off", debug="on", shader_print="off", warnings="on", opt="none"),
}

VAR_RE = re.compile(r'^(\w+)\s*:=\s*"((?:[^"\\]|\\[\s\S])*)"', re.M)


def read_justfile(root):
    path = os.path.join(root, "justfile")
    with open(path, encoding="utf-8") as f:
        return f.read()


def parse_vars(text):
    """Collect `name := "..."` string variables, joining line continuations.

    justfile strings use backslash-newline for continuation and split on
    whitespace, so the joined body is a plain argv fragment.
    """
    out = {}
    for name, body in VAR_RE.findall(text):
        toks = body.replace("\\\n", " ").split()
        out[name] = toks
    return out


def profile_flags(v, profile):
    """Expand the same conditionals `buildn` applies for one profile."""
    p = PROFILES[profile]
    flags = []
    if p["warnings"] == "on":
        flags += v["compwarn"]
    if p["debug"] == "on":
        flags += v["compdebug"]
    if p["sanitize"] == "on":
        flags += v["compsanitize"]
    flags += v["compfast"]
    flags += v.get("complibs", [])
    flags += v["complogall"] if p["verbosity"] in ("all", "a") else []
    flags += v["complogdebug"] if p["verbosity"] in ("debug", "d") else []
    flags += v["complogwarn"] if p["verbosity"] in ("warn", "w") else []
    flags += v["complogdist"] if p["verbosity"] == "dist" else []
    flags += ["-D", "__SHADER_PRINT"] if p["shader_print"] == "on" else []
    flags += v["compoptheavy"] if p["opt"] in ("h", "heavy") else []
    flags += v["corebuild"]
    return flags


# Flags that only matter when linking or producing an object file. clangd
# rejects or warns about them, and they change nothing about parsing.
LINK_ONLY = re.compile(
    r"^(?:-l|/lib/|/usr/lib/|-fsanitize=|-fsanitize-trap=|--warning-suppression-mappings=|"
    r"-fPIC$|-flto$|-march=native$|-o$)"
)


def strip_for_clangd(flags, source):
    """Drop link-only flags and the language override.

    `-x c` is dropped deliberately: it applies to every file clangd opens,
    so headers get parsed as main files (-Wpragma-once-outside-header). clangd
    infers the language from the extension instead.
    """
    out = []
    i = 0
    while i < len(flags):
        f = flags[i]
        if f == "-x":
            i += 2  # skip the flag and its operand
            continue
        if LINK_ONLY.match(f):
            i += 1
            continue
        out.append(f)
        i += 1
    return out


def sources(root, explicit):
    if explicit:
        return [os.path.join(root, s) for s in explicit]
    srcdir = os.path.join(root, "src")
    found = []
    for name in sorted(os.listdir(srcdir)):
        if name.endswith(".c"):
            found.append(os.path.join(srcdir, name))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--compiler", default="clang", choices=["clang", "gcc"])
    ap.add_argument("--profile", default="build", choices=sorted(PROFILES))
    ap.add_argument("--source", action="append", help="translation unit; repeatable")
    ap.add_argument("--out", default="compile_commands.json")
    ap.add_argument("--root", default=ROOT)
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    variables = parse_vars(read_justfile(root))
    missing = [n for n in ("compwarn", "compdebug", "corebuild") if n not in variables]
    if missing:
        sys.exit(f"justfile is missing expected variables: {', '.join(missing)}")

    flags = profile_flags(variables, args.profile)
    driver = "clang" if args.compiler == "clang" else "gcc"

    entries = []
    for src in sources(root, args.source):
        if not os.path.exists(src):
            continue
        rel = os.path.relpath(src, root)
        argv = [driver] + strip_for_clangd(flags, rel) + [rel]
        entries.append(
            {"directory": root, "file": rel, "arguments": argv, "output": rel + ".o"}
        )

    if not entries:
        sys.exit("no translation units found")

    out = args.out if os.path.isabs(args.out) else os.path.join(root, args.out)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(entries, f, indent=2)
        f.write("\n")
    print(f"wrote {len(entries)} entries to {out}")
    print(f"  compiler={driver} profile={args.profile}")
    for e in entries:
        print("  " + e["file"])


if __name__ == "__main__":
    main()
