"""Create a board-specific, CRC-protected USB update and a first-install image."""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BOARD, BASE, META, VERSION = 0x53443231, 0x4000, 0x3FC00, 0x00030000

def package(data, version=VERSION):
    if not 192 <= len(data) <= META-BASE:
        raise ValueError('Application size is outside the reserved flash region')
    sp, pc = struct.unpack_from('<II', data)
    if not (0x20000000 < sp <= 0x20008000 and sp % 8 == 0 and pc & 1
            and BASE <= (pc & ~1) < BASE+len(data)):
        raise ValueError('Application must be linked at 0x4000 for SAMD21E18A')
    crc = zlib.crc32(data)
    header = struct.pack('<8s5I32x', b'CWFWPKG1', BOARD, BASE, len(data), crc, version)
    return header+struct.pack('<I', zlib.crc32(header))+data

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--application', type=Path, default=ROOT/'usb-audio/build/Coral-Wave.bin')
    p.add_argument('--bootloader', type=Path, default=ROOT/'bootloader/build/Coral-Wave-Bootloader.bin')
    p.add_argument('--output', type=Path, default=ROOT/'releases')
    a = p.parse_args()
    data, boot = a.application.read_bytes(), a.bootloader.read_bytes()
    if len(boot)>BASE: raise ValueError('Bootloader exceeds 16 KiB')
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output/'Coral-Wave.cwfw').write_bytes(package(data))
    (a.output/'Coral-Wave-USB.bin').write_bytes(data)
    (a.output/'Coral-Wave-Bootloader.bin').write_bytes(boot)
    image=bytearray(b'\xff'*(META+256))
    image[:len(boot)]=boot
    image[BASE:BASE+len(data)]=data
    metadata=struct.pack('<6I',0x57415645,len(data),zlib.crc32(data),VERSION,BOARD,BASE)
    image[META:META+28]=metadata+struct.pack('<I',zlib.crc32(metadata))
    (a.output/'Coral-Wave-First-Install.bin').write_bytes(image)
    (a.output/'Coral-Wave-USB.hex').write_bytes((ROOT/'usb-audio/build/Coral-Wave.hex').read_bytes())
    files={f.name:{'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
           for f in a.output.iterdir() if f.suffix in ('.bin','.hex','.cwfw','.exe')}
    (a.output/'manifest.json').write_text(json.dumps({
        'version':'3.0.0','board':'Coral-Wave / ATSAMD21E18A + WM8978',
        'application_address':'0x00004000','bootloader_bytes':16384,
        'usb_update':'Coral-Wave.cwfw','first_install':'Coral-Wave-First-Install.bin',
        'files':files},ensure_ascii=False,indent=2)+'\n', encoding='utf-8')
    print('Packaged 3.0.0, application',len(data),'bytes, bootloader',len(boot),'bytes')

if __name__=='__main__': main()
