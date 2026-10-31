# md_create

- `md_create`/L0 record source runs and blocks with source metadata, not display-ready content.
- L0 ranges form an ordered, non-overlapping partition of `[0, source->total_length)`: every source byte is covered exactly once, including line endings and the unterminated EOF tail.
- L0 ends with a tagged `MD_L0_EOF` boundary at `source->total_length`, included in `l0_count`. Source lengths are adjacent-offset differences. During creation, output arrays are write-only; pending line metadata and blank counts stay in local state.
- The first terminating LF belongs to the preceding line; subsequent blank-line bytes belong to the blank run, including its final LF.
- Preserve source facts in metadata: indentation, marker byte/length, padding, and physical line counts. Do not trim or normalize source ranges for rendering.
- `md_get` interprets these records and derives semantic nodes and separate content ranges. Source ranges include bytes that content ranges exclude.
- Keep V2 migration edits small and focused; do not bundle unrelated control-flow or semantic rewrites.
