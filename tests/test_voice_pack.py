import importlib.util
import tempfile
import unittest
from pathlib import Path
import struct
import zlib
import json
import hashlib
import sys
from types import SimpleNamespace
ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('pack', ROOT/'tools/build_voice_pack.py')
sys.path.insert(0,str(ROOT/'tools'))
from verify_voice_bundle import verify_voice_bundle
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)

class VoicePackTests(unittest.TestCase):
    def test_approved_selection_and_retained_audio(self):
        assets=ROOT/'assets/audio/voice-keychain'
        index=json.loads((assets/'voice_index.json').read_text(encoding='utf-8'))
        manifest=json.loads((assets/'selection.json').read_text(encoding='utf-8'))
        all_paths={f['path'] for p in index for f in p['files']}
        paths={f['path'] for p in index if p.get('storage','partition')=='partition' for f in p['files']}
        self.assertEqual(len(index),25)
        self.assertEqual(len(all_paths),709)
        self.assertEqual(len(paths),695)
        self.assertEqual(index[0]['dir'],'高燃BGM')
        self.assertEqual([item['name'] for item in index[0]['files']],
                         ['囚笼','Amazon压迫感','伏黑甚尔进行曲','Try','小紫万花筒进行曲',
                          'Overturn反转进行曲','反派烧气救场','DeathIsNoMore','SubTitle',
                          'MySunset','毁灭Destroy','MyWay','审判时刻','UNAMASFUNK2'])
        later=json.loads((assets/'long-selection.json').read_text(encoding='utf-8'))
        later_removed={x['path'] for x in later['removed']}
        self.assertEqual(len(manifest['groups']),46)
        removed=[];seen=set()
        for group in manifest['groups']:
            self.assertEqual(sum(f['path'] in paths for f in group['items']),0 if group['keep'] in later_removed else 1)
            self.assertIn(group['keep'],paths|later_removed)
            for item in group['items']:
                self.assertNotIn(item['path'],seen);seen.add(item['path'])
                path=assets/item['path']
                if item['path']==group['keep']:
                    if item['path'] in later_removed:
                        self.assertFalse(path.exists())
                        self.assertEqual(later['before_assets'][item['path']]['sha256'],item['sha'])
                    else:self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),item['sha'])
                else:
                    self.assertFalse(path.exists());removed.append(item)
        self.assertEqual(len(removed),92)
        self.assertEqual(sum(f['compressed_bytes'] for f in removed),633772)
        self.assertEqual(manifest['before_bytes']-manifest['after_bytes'],633772)

    def test_long_selection_keeps_exact_requested_eight(self):
        assets=ROOT/'assets/audio/voice-keychain'
        manifest=json.loads((assets/'long-selection.json').read_text(encoding='utf-8'))
        expected={('宝宝肚肚打雷','rap'),('纸片人捷风','挨刀马DJ2'),('王世坚','《没出息》'),
                  ('哈基米','哈基歌'),('高松灯','咪咪咪'),('高松灯','天空'),('鸡乐','只因你太美'),('哈基米','漫波歌')}
        self.assertEqual({(x['category'],x['name']) for x in manifest['kept']},expected)
        self.assertEqual({x['id'] for x in manifest['kept']},{4,7,8,14,19,24,25,29})
        removed={x['path'] for x in manifest['removed']}
        self.assertEqual(len(removed),31)
        self.assertTrue(all(x['seconds']>=15 for x in manifest['removed']))
        index=json.loads((assets/'voice_index.json').read_text(encoding='utf-8'))
        paths={x['path'] for p in index if p.get('storage','partition')=='partition' for x in p['files']}
        all_paths={x['path'] for p in index for x in p['files']}
        self.assertEqual(paths,set(manifest['before_assets'])-removed)
        self.assertEqual(all_paths,{p.relative_to(assets).as_posix() for p in assets.rglob('*.opus')})
        self.assertTrue({x['path'] for x in manifest['kept']}<=paths)
        self.assertEqual(sum(manifest['before_assets'][p]['bytes'] for p in removed),977379)
        for p in paths:
            data=(assets/p).read_bytes()
            self.assertEqual(len(data),manifest['before_assets'][p]['bytes'])
            self.assertEqual(hashlib.sha256(data).hexdigest(),manifest['before_assets'][p]['sha256'])

    def test_bad_packets(self):
        for value in [b'\x01', b'\x00\x00', b'\xff\xff', b'\x04\x00abc']:
            with self.assertRaises(ValueError):
                list(pack.packets(value))
        self.assertEqual(list(pack.packets(b'\x03\x00abc')), [b'abc'])

    def test_complete_bundle(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            pack.build(d)
            a, b = (d/'voice_data.bin').read_bytes(), (d/'voice_tail.bin').read_bytes()
            self.assertEqual((len(a), len(b)), (pack.FIRST, pack.TAIL))
            h = a[:28]
            self.assertEqual(struct.unpack('<4sII', h[:12]), (b'VKP1',1,695))
            size, _, crc, header_crc = struct.unpack('<IIII', h[12:])
            self.assertEqual(zlib.crc32(h[:24]), header_crc)
            self.assertEqual(zlib.crc32((a+b)[4096:4096+size]), crc)
            self.assertEqual(size, 3293101)
            index=json.loads((ROOT/'assets/audio/voice-keychain/voice_index.json').read_text(encoding='utf-8'))
            embedded=b''.join((ROOT/'assets/audio/voice-keychain'/item['path']).read_bytes()
                              for group in index if group.get('storage')=='app' for item in group['files'])
            self.assertEqual((d/'voice_bgm.bin').read_bytes(),embedded)
            self.assertEqual(len(embedded),494158)
            parts=[SimpleNamespace(label='voice_data',offset=0x3d0000,size=pack.FIRST),SimpleNamespace(label='voice_tail',offset=0x750000,size=pack.TAIL)]
            images={'voice_data.bin':0x3d0000,'voice_tail.bin':0x750000}
            merged=bytearray(b'\xff'*0x7d0000)
            merged[0x3d0000:0x690000]=a;merged[0x750000:0x7d0000]=b
            verify_voice_bundle(d,images,parts,merged)
            with self.assertRaises(ValueError):verify_voice_bundle(d,{},parts,merged)
            damaged=bytearray(a);damaged[4096]^=1
            (d/'voice_data.bin').write_bytes(damaged);merged[0x3d0000:0x690000]=damaged
            with self.assertRaisesRegex(ValueError,'CRC'):verify_voice_bundle(d,images,parts,merged)
            pack.build(d)
            self.assertEqual(a, (d/'voice_data.bin').read_bytes())
            self.assertEqual(b, (d/'voice_tail.bin').read_bytes())

if __name__ == '__main__':
    unittest.main()
