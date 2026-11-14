#!/usr/bin/env python3
"""Production-only API-refactor baseline; reuse the existing benchmark/regression harnesses.

python tests/fontshaper_api_baseline.py --output /tmp/fontshaper-api-baseline.json
python tests/fontshaper_api_baseline.py --check tests/data/fontshaper_api_baseline.json

Timings include UTF-8 context shaping, flat copying/source lookup and the existing
simple English wrapper. Output fingerprints exclude timings; no ASCII spike is built.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def run(command):
    return subprocess.check_output(command, text=True)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('/tmp/fontshaper-api-baseline.json'))
    parser.add_argument('--check', type=Path, help='Compare outputs/font hashes, not machine-dependent timings')
    parser.add_argument('--managed', action='store_true', help='Benchmark fixed-partition states in caller-owned storage')
    parser.add_argument('--batch', action='store_true', help='Benchmark whole-buffer shaping (implies --managed)')
    args = parser.parse_args()
    if args.batch:
        args.managed = True
    fonts = {
        'NotoSans': '/usr/share/fonts/TTF/NotoSansNerdFontPropo-Regular.ttf',
        'Inter': '/usr/share/fonts/Inter Regular Nerd Font Complete.otf',
        'NotoSerif': '/usr/share/fonts/TTF/NotoSerifNerdFont-Regular.ttf',
        'JetBrainsMono': '/usr/share/fonts/TTF/JetBrainsMonoNerdFont-Regular.ttf',
    }
    ordinary = ('A nonexistent file opens as an empty document and is created on save. '
                'Invalid UTF-8 or other load failures are reported without overwriting the file. '
                'An untitled sample needs a file path on launch to enable saving; no save-as dialog is provided yet.')
    texts = {
        'heading': 'A practical guide to building a native Markdown editor',
        'ordinary': ordinary,
        'long': ' '.join([ordinary] * 4),
        'ligatures': ('The efficient office staff finally finished the difficult filing. A fine affinity for fluent typography offers sufficient flexibility. '
                      'The offline configuration specifies official figures and confirms that the first fifty files are finished.'),
        'code': 'if (left != right && count <= capacity) { output[index++] = value; } // -> => === !== <= >= != && || ++ -- :: ... <=>',
        'combining': 'Cafe\u0301, naïve, Ελληνικά, office fi AV.',
        'controls': 'First\tline\r\n\r\nSecond line and office fi.',
        'block_4k': '\n\n'.join([ordinary] * 16),
    }
    capacity = 8192
    cpu = min(os.sched_getaffinity(0))
    cc = os.environ.get('CC', 'clang')
    flags = ['-O3', '-march=native', '-ffunction-sections', '-fdata-sections',
             '-DBENCH_CAPACITY=' + str(capacity), '-I', str(root / 'src')]
    if args.managed:
        flags.append('-DFONTSHAPER_BENCH_MANAGED')
    if args.batch:
        flags.append('-DFONTSHAPER_BENCH_BATCH')
    font_hashes = {name: sha256(Path(path).read_bytes()) for name, path in fonts.items()}
    replay_font_hashes = {path: sha256(Path(path).read_bytes()) for path in [
        fonts['NotoSerif'], '/usr/share/fonts/noto/NotoSansArabic-Regular.ttf',
        '/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf',
    ]}
    metadata = dict(
        compiler=run([cc, '--version']).splitlines()[0], flags=flags, cpu=cpu,
        cpu_model=next(line.split(':', 1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines()
                       if line.startswith('model name')),
        git_head=run(['git', '-C', str(root), 'rev-parse', 'HEAD']).strip(),
        header_sha256=sha256((root / 'src/lib/fontshaper/fontshaper.h').read_bytes()),
        input_capacity=capacity, glyph_capacity=capacity, cache_budget=8 * 1024 * 1024,
        fonts=fonts, font_sha256=font_hashes, replay_font_sha256=replay_font_hashes, inputs=texts,
        harness_sha256={name: sha256((root / 'tests' / name).read_bytes()) for name in [
            'fontshaper_api_baseline.py', 'fontshaper_ascii_spike_bench.c',
            'fontshaper_ascii_replay_test.c', 'fontshaper_context_test.c',
        ]},
        scope='Production UTF-8 state + source resolution/flat copy + English wrapping at 640px/18px. Five warm batches; no spike.',
        cold_scope='font_load_us includes loading/compilation; setup_us includes legacy test-fixture creation/resizing (two scratch buffers); first_us is first paragraph, including config construction.',
        output_format='Benchmark lines: glyph ID/codepoint/codepoint index/source ID/advances/offsets/wrapped XY; R script/directions/flags/input bounds; B codepoint/break flags/script/directions/source ID. Replay adds font/mapping identity, manual input, malformed UTF-8 and multilingual cases.',
    )
    metadata['ownership'] = 'expert separate buffers'
    if args.managed:
        metadata['ownership'] = 'caller-owned single block; no allocator callbacks retained'
        metadata['cold_scope'] = 'font_load_us includes loading/compilation; setup_us includes size query, caller allocation and creation (which revalidates all-language bounds); first_us includes lazy config construction.'
        metadata['scope'] = 'Incremental UTF-8 preparation + eager shaping/source finalization + flat copy + English wrapping at 640px/18px. Five warm batches; no spike.'
    if args.batch:
        metadata['scope'] = 'Whole-buffer UTF-8 preparation + eager shaping/source finalization + flat copy + English wrapping at 640px/18px. Five warm batches; no spike.'
    expected = None
    if args.check:
        expected = json.loads(args.check.read_text())
        assert expected['metadata']['font_sha256'] == font_hashes, 'Font files differ from the reference baseline'
        assert expected['metadata']['replay_font_sha256'] == replay_font_hashes, 'Replay fonts differ from the reference baseline'
        assert expected['metadata']['inputs'] == texts, 'Baseline inputs changed'
        assert expected['metadata']['glyph_capacity'] == capacity, 'Baseline capacity changed'
    rows = []
    with tempfile.TemporaryDirectory(prefix='fontshaper-api-baseline-') as directory:
        work = Path(directory)
        binaries = {}
        for name, source in [('bench', 'fontshaper_ascii_spike_bench.c'),
                             ('replay', 'fontshaper_ascii_replay_test.c')]:
            binary = str(work / name)
            subprocess.run([cc, *flags, str(root / 'tests' / source), '-Wl,--gc-sections', '-lm', '-o', binary], check=True)
            binaries[name] = binary
        replay = run([binaries['replay']])
        metadata['replay_sha256'] = sha256(replay.encode())
        if expected:
            assert metadata['replay_sha256'] == expected['metadata']['replay_sha256'], 'Glyph/break/run/fallback replay output changed'
        for font, font_path in fonts.items():
            for text_name, text in texts.items():
                text_path = work / 'input.txt'
                text_path.write_text(text, encoding='utf-8')
                for overrides in [0, 1]:
                    output = run(['taskset', '-c', str(cpu), binaries['bench'], font_path, str(text_path), str(overrides)])
                    first, records = output.split('\n', 1)
                    fields = dict(re.findall(r'(\w+)=([^\s]+)', first))
                    samples = [float(value) for value in fields.pop('times_us').strip(',').split(',')]
                    row = dict(font=font, text=text_name, overrides=overrides,
                               median_us=statistics.median(samples), times_us=samples,
                               output_sha256=sha256(records.encode()))
                    for key, value in fields.items():
                        if key.endswith('_us'):
                            row[key] = float(value)
                        else:
                            row[key] = int(value)
                    if expected:
                        reference = next(item for item in expected['results'] if
                                         (item['font'], item['text'], item['overrides']) == (font, text_name, overrides))
                        assert row['output_sha256'] == reference['output_sha256'], (font, text_name, overrides, 'Output changed')
                    rows.append(row)
                    print(f'{font:13} {text_name:10} overrides={overrides} warm={row["median_us"]:.3f}us first={row["first_us"]:.3f}us', flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(metadata=metadata, results=rows), indent=2) + '\n')
    print(f'{len(rows)} output fingerprints; full Unicode/manual-run replay; stable cache checked in every batch. Saved {args.output}')


if __name__ == '__main__':
    main()
