#!/usr/bin/env python3
"""Guarded ASCII/replay spike versus production; no third-party Python packages."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=Path('/tmp/fontshaper-ascii-spike-results.json'))
args = parser.parse_args()
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
    'short': 'The editor renders each paragraph independently. Most text is plain English, with occasional office ligatures and a little punctuation.',
    'ordinary': ordinary,
    'long': ' '.join([ordinary] * 4),
    'early_miss': 'Café. ' + ordinary,
    'late_miss': ordinary + ' Café.',
    'ligatures': ('The efficient office staff finally finished the difficult filing. A fine affinity for fluent typography offers sufficient flexibility. '
                  'The offline configuration specifies official figures and confirms that the first fifty files are finished.'),
    'code': 'if (left != right && count <= capacity) { output[index++] = value; } // -> => === !== <= >= != && || ++ -- :: ... <=>',
}
rows = []
checks = 0
cpu = min(os.sched_getaffinity(0))
metadata = dict(cpu=cpu, compiler=subprocess.check_output([os.environ.get('CC', 'clang'), '--version'], text=True).splitlines()[0],
                scope='Guarded spike: warm UTF-8 context, flat copying/source lookup, English wrapping at 640px/18px; full final break/run metadata checked.',
                inputs=texts, fonts=fonts, headers={})
for name in ['fontshaper.h', 'fontshaper_ascii_spike.h']:
    metadata['headers'][name] = hashlib.sha256((root / 'src/lib/fontshaper' / name).read_bytes()).hexdigest()
with tempfile.TemporaryDirectory(prefix='fontshaper-ascii-spike-') as directory:
    work = Path(directory)
    replay_outputs = {}
    for name, header in [('normal', 'fontshaper.h'), ('spike', 'fontshaper_ascii_spike.h')]:
        flags = [os.environ.get('CC', 'clang'), '-O3', '-march=native', '-ffunction-sections', '-fdata-sections',
                 '-I', str(root / 'src'), '-DFONTSHAPER_TEST_HEADER="lib/fontshaper/' + header + '"']
        subprocess.run(flags + [str(root / 'tests/fontshaper_ascii_spike_bench.c'), '-Wl,--gc-sections', '-lm', '-o', str(work / name)], check=True)
        if name == 'spike': flags.append('-DSPIKE_TEST')
        replay_binary = str(work / (name + '-replay'))
        subprocess.run(flags + [str(root / 'tests/fontshaper_ascii_replay_test.c'), '-Wl,--gc-sections', '-lm', '-o', replay_binary], check=True)
        replay_outputs[name] = subprocess.check_output([replay_binary], text=True)
    assert replay_outputs['normal'] == replay_outputs['spike'], 'fallback glyph/break/run metadata mismatch'
    print('Guard and fallback metadata comparisons passed.', flush=True)
    for font, font_path in fonts.items():
        for text_name, text in texts.items():
            text_path = work / (text_name + '.txt')
            text_path.write_text(text, encoding='utf-8')
            for overrides in [0, 1]:
                names = ['normal', 'spike']
                if checks % 2: names.reverse()
                outputs = {}
                pair = {}
                for name in names:
                    output = subprocess.check_output(['taskset', '-c', str(cpu), str(work / name), font_path, str(text_path), str(overrides)], text=True)
                    lines = output.splitlines()
                    fields = dict(re.findall(r'(\w+)=([^\s]+)', lines[0]))
                    median = statistics.median(float(v) for v in fields['times_us'].strip(',').split(','))
                    outputs[name] = lines[1:]
                    pair[name] = median
                    rows.append(dict(font=font, text=text_name, overrides=overrides, variant=name, median_us=median, **fields))
                assert outputs['normal'] == outputs['spike'], (font, text_name, overrides, 'glyph/source/position/break/run mismatch')
                checks += 1
                print(f'{font:13} {text_name:10} overrides={overrides} normal={pair["normal"]:.3f}us spike={pair["spike"]:.3f}us saved={pair["normal"] - pair["spike"]:.3f}us ({100 * (1 - pair["spike"] / pair["normal"]):.1f}%)', flush=True)
                args.output.write_text(json.dumps(dict(metadata=metadata, checked_pairs=checks, results=rows), indent=2))
print(f'All {checks} glyph/source/position/break/run comparisons passed; stable cache usage checked in every batch. Results: {args.output}')
