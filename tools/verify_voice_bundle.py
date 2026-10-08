"""Product-specific voice data verification, separate from generic board layout."""
import struct
import zlib

def verify_voice_bundle(build_dir, images, partitions, merged):
    labels={p.label:p for p in partitions}
    if 'voice_data' not in labels and 'voice_tail' not in labels:
        return
    pieces=[]
    for name,offset,size in [('voice_data',0x3d0000,0x2c0000),('voice_tail',0x750000,0x80000)]:
        p=labels.get(name)
        if not p or p.offset!=offset or p.size!=size or images.get(name+'.bin')!=offset:
            raise ValueError('Voice image/partition mapping is incomplete or inconsistent')
        data=(build_dir/(name+'.bin')).read_bytes()
        if len(data)!=size or merged[offset:offset+size]!=data:
            raise ValueError('Voice image size or merged content mismatch')
        pieces.append(data)
    bundle=b''.join(pieces)
    magic,version,clips,size,identity,crc,header_crc=struct.unpack('<4sIIIIII',bundle[:28])
    if (magic,version)!=(b'VKP1',1) or clips<1 or size>len(bundle)-4096:
        raise ValueError('Invalid voice resource header')
    if zlib.crc32(bundle[:24])!=header_crc or zlib.crc32(bundle[4096:4096+size])!=crc:
        raise ValueError('Voice resource CRC mismatch')
    catalog=(build_dir/'voice_catalog.h').read_text()
    if (f'#define VOICE_PARTITION_CATALOG_CRC {identity}u' not in catalog
        or f'#define VOICE_PARTITION_CLIP_COUNT {clips}u' not in catalog
        or f'#define VOICE_PAYLOAD_BYTES {size}u' not in catalog):
        raise ValueError('Firmware catalog and audio bundle disagree')
    print(f'Voice resources: PASS ({clips} clips / {size} bytes, both partitions verified)')
