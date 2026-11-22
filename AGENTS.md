# Coding rules

- Do not use ternary (`?:`) operators. Use explicit `if`/`else` statements instead.
- Never use `++`, prefix or postfix. Use `+= 1` instead.
- Never use `!var` for NON boolean variables. Use `var == 0` explicitly to check for 0.
- Never run scripts or shell commands to edit C source or header files. Use the edit tool for targeted, reviewable changes.
- TODO, NOTE, PERF etc comments: `// TODO some text` DO NOT put a colon after the tag: `// TODO: some text` IS WRONG.

