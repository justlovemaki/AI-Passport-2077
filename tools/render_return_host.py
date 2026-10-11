#!/usr/bin/env python3
"""Render/test the production return overlay without ESP-IDF or hardware.

Run from a POSIX host with gcc and the resolved LVGL component. Artifacts and
object cache stay in build/return-ui; no reference image is silently rebased.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LVGL = ROOT / 'managed_components/lvgl__lvgl'
OUT = ROOT / 'build/return-ui'
OUT.mkdir(parents=True, exist_ok=True)
config = '''#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (32 * 1024)
#define LV_USE_LOG 1
#define LV_LOG_PRINTF 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_MEM_INTEGRITY 1
'''
conf = OUT / 'lv_conf.h'
if not conf.exists() or conf.read_text() != config:
    conf.write_text(config)
# The representative Muyu screen needs only the authoritative LCD dimensions,
# not a host copy of ESP-IDF's driver headers.
pins = (ROOT / 'components/bsp/include/bsp_pins.h').read_text()
pin_header = '\n'.join(re.findall(r'^#define BSP_LCD_[WH]\s+\d+', pins, re.M)) + '\n'
pin_path = OUT / 'bsp_pins.h'
if not pin_path.exists() or pin_path.read_text() != pin_header:
    pin_path.write_text(pin_header)
cc = os.environ.get('CC', 'gcc')
flags = ['-O0', '-ffunction-sections', '-fdata-sections', '-DLV_CONF_INCLUDE_SIMPLE',
         f'-I{OUT}', f'-I{LVGL}', f'-I{ROOT / "main"}']
sources = list((LVGL / 'src').rglob('*.c')) + [ROOT / 'main' / name for name in (
    'badge_ui.c', 'badge_navigation.c', 'badge_theme.c',
    'font_ui_14.c', 'font_muyu_14.c', 'font_badge_10.c', 'font_badge_28.c',
    'muyu_ui.c', 'muyu_logic.c', 'font_muyu_22.c', 'badge_header.c')]
sources.append(ROOT / 'tests/test_badge_return_ui.c')
headers = list((ROOT / 'main').glob('*.h'))
header_time = max(path.stat().st_mtime for path in headers)

def compile_source(path):
    obj = OUT / (hashlib.sha256(str(path).encode()).hexdigest()[:16] + '.o')
    dependency_time = max(path.stat().st_mtime, conf.stat().st_mtime, pin_path.stat().st_mtime)
    if path.is_relative_to(ROOT / 'main') or path.is_relative_to(ROOT / 'tests'):
        dependency_time = max(dependency_time, header_time)
    if not obj.exists() or obj.stat().st_mtime < dependency_time:
        subprocess.run([cc, *flags, '-c', str(path), '-o', str(obj)], check=True)
    return str(obj)

with ThreadPoolExecutor(max_workers=8) as pool:
    objects = list(pool.map(compile_source, sources))
executable = OUT / 'test-return-ui'
subprocess.run([cc, *objects, '-Wl,--gc-sections', '-lm', '-o', str(executable)], check=True)
subprocess.run([str(executable), str(OUT)], cwd=ROOT, check=True, timeout=60)
