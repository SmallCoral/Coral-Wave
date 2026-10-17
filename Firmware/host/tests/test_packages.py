"""Reject malformed images before a USB device can be reset or erased."""
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
CLI=ROOT/'host/build/Coral-Wave-CLI.exe'
GOOD=(ROOT/'releases/Coral-Wave.cwfw').read_bytes()

def checksums(image):
    struct.pack_into('<I',image,20,zlib.crc32(image[64:]))
    struct.pack_into('<I',image,60,zlib.crc32(image[:60]))
    return image

class Packages(unittest.TestCase):
    def check(self,image,valid=False):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'test.cwfw'
            path.write_bytes(image)
            result=subprocess.run([str(CLI),'--validate',str(path)],capture_output=True)
            self.assertEqual(result.returncode,0 if valid else 1,result.stdout+result.stderr)
    def test_release(self): self.check(GOOD,True)
    def test_raw_first_install(self): self.check((ROOT/'releases/Coral-Wave-First-Install.bin').read_bytes())
    def test_truncated(self): self.check(GOOD[:-1])
    def test_corrupt_data(self):
        bad=bytearray(GOOD);bad[-1]^=1;self.check(bad)
    def test_corrupt_header(self):
        bad=bytearray(GOOD);bad[24]^=1;self.check(bad)
    def test_other_board(self):
        bad=bytearray(GOOD);struct.pack_into('<I',bad,8,0x12345678);self.check(checksums(bad))
    def test_zero_flash_address(self):
        bad=bytearray(GOOD);struct.pack_into('<I',bad,12,0);self.check(checksums(bad))
    def test_bootloader_entry(self):
        bad=bytearray(GOOD);struct.pack_into('<I',bad,68,0x101);self.check(checksums(bad))
    def test_invalid_stack(self):
        bad=bytearray(GOOD);struct.pack_into('<I',bad,64,0x20010000);self.check(checksums(bad))
    def test_wrapping_length(self):
        bad=bytearray(GOOD);struct.pack_into('<I',bad,16,0xFFFFFFFF);self.check(checksums(bad))

if __name__=='__main__': unittest.main()
