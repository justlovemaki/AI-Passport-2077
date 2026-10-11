#!/usr/bin/env python3
"""Check actual generated menu text against the committed font's cmap."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def coverage(font, text):
    source = (ROOT / 'main' / font).read_text(encoding='utf-8')
    if 'font_ui_14_descriptor' in source:
        source = (ROOT/'main/font_ui_14.c').read_text(encoding='utf-8')
    offsets = re.search(r'uint16_t unicode\[\]\s*=\s*\{([^}]+)\}', source)[1]
    start = int(re.search(r'\.range_start=(\d+)', source)[1])
    glyphs = {chr(start + int(n)) for n in offsets.split(',')}
    missing = sorted(set(text) - glyphs - {'\n', '\r', '\t'})
    assert not missing, f'{font}: missing glyphs {missing}'

generated = (Path(sys.argv[1]) / 'voice_catalog.h').read_text(encoding='utf-8')
summary = re.search(r'#define VOICE_SUMMARY "([^"]+)"', generated)[1]
coverage('font_muyu_14.c', '音效钥匙扣' + summary)
voice_index = json.loads((ROOT / 'assets/audio/voice-keychain/voice_index.json').read_text(encoding='utf-8'))
coverage('font_voice_14.c', ''.join(pack['dir'] + ''.join(item['name'] for item in pack['files']) for pack in voice_index))
presets = (ROOT / 'main/radio_presets.cc').read_text(encoding='utf-8')
coverage('font_radio_14.c', ''.join(re.findall(r'\{"([^"]+)"', presets)))
registry = (ROOT / 'main/game_registry.c').read_text(encoding='utf-8')
app_ids = re.findall(r'\.id="([^"]+)"', registry)
assert app_ids and app_ids[0] == 'muse', app_ids
shengbei = (ROOT / 'main/shengbei_app.c').read_text(encoding='utf-8')
coverage('font_muyu_14.c', ''.join(re.findall(r'"([^"]*[\u3000-\u9fff][^"]*)"', registry + shengbei)))
coverage('font_badge_28.c', '圣杯笑阴阳未投掷')
return_sources = ''
for name, start, end in (
    ('badge_navigation.c', 'void badge_return_menu_failed', 'void badge_navigation_badges'),
    ('badge_ui.c', 'static const char *return_destination', 'static void setup_steps'),
    ('main.c', 'static bool query_return', 'static nvs_handle_t settings'),
    ('voice_app.c', 'bool demo_voice_return_context', 'bool demo_voice_back'),
    ('radio_app.cc', 'extern "C" bool demo_radio_return_context', 'extern "C" bool demo_radio_back'),
    ('muse_app.cc', 'extern "C" bool demo_muse_return_context', 'extern "C" void demo_muse_key'),
    ('xiaozhi_app.cc', 'extern "C" bool demo_xiaozhi_return_context', 'extern "C" bool demo_xiaozhi_active'),
):
    source = (ROOT / 'main' / name).read_text(encoding='utf-8')
    return_sources += source[source.index(start):source.index(end)]
# Modal text always uses the shared UI14 font, including above AI/Muse screens.
coverage('font_muyu_14.c', ''.join(re.findall(r'"([^"\n]*[\u3000-\u9fff][^"\n]*)"', return_sources)))
print('Menu, return chooser and preset labels: font coverage PASS')

# The complete control legend must fit at once, including long-press actions.
footer = (ROOT / 'main/badge_footer.h').read_text(encoding='utf-8')
hints = re.findall(r'#define BADGE_HINT_\w+ "([^"\n]+)"', footer)
font = (ROOT / 'main/font_badge_10.c').read_text(encoding='utf-8')
start = int(re.search(r'\.range_start=(\d+)', font)[1])
offsets = re.search(r'uint16_t unicode\[\]\s*=\s*\{([^}]+)\}', font)[1]
chars = [chr(start + int(n)) for n in offsets.split(',')]
advances = [int(n) for n in re.findall(r'\.adv_w=(\d+)', font)]
assert len(chars) == len(advances)
widths = dict(zip(chars, advances))
assert hints
for hint in hints:
    coverage('font_badge_10.c', hint)
    assert '\\n' not in hint
    assert sum(widths[c] for c in hint) <= 224 * 16, f'Footer too wide: {hint}'
print('All footer actions: glyph coverage and single-line width PASS')

# Narrow Latin glyphs must not inherit a fixed-width cell. Match LVGL pixel rounding.
assert widths['I'] < widths['M'] and widths['i'] < widths['W']
def pixel_width(text): return sum((widths[c]+8)//16 for c in text)
assert pixel_width('AI Passport') <= 78
assert pixel_width('100%') <= 32
assert pixel_width('23:59') <= 54
for hint in hints: assert pixel_width(hint) <= 224, hint
print('Proportional Latin spacing, battery/header and rounded footer widths PASS')
