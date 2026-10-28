"""Check that a CC2652P firmware image (.hex or .bin) keeps the ROM serial bootloader usable.

Usage: python3 check_ccfg.py firmware.hex [--pin 15] [--flash-kb 352]

The ROM bootloader itself cannot be erased, but the CCFG block (last 0x58 bytes of flash)
decides whether it can be entered. If BOOTLOADER_ENABLE or the backdoor is disabled in the
new image, USB re-flashing is lost until a JTAG/cJTAG probe is used.
"""
import argparse, sys

def load(path, size):
    img = bytearray(b"\xff" * size)
    if not path.lower().endswith(".hex"):
        data = open(path, "rb").read()
        img[:len(data)] = data[:size]
        return img
    base = 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        rec = bytes.fromhex(line[1:])
        n, addr, typ, data = rec[0], int.from_bytes(rec[1:3], "big"), rec[3], rec[4:4 + rec[0]]
        if typ == 0x00:
            a = base + addr
            if a < size:
                img[a:a + n] = data
        elif typ == 0x02:
            base = int.from_bytes(data, "big") << 4
        elif typ == 0x04:
            base = int.from_bytes(data, "big") << 16
    return img

ap = argparse.ArgumentParser()
ap.add_argument("image")
ap.add_argument("--pin", type=int, default=15, help="expected backdoor DIO (BOOT button)")
ap.add_argument("--flash-kb", type=int, default=352)
args = ap.parse_args()

size = args.flash_kb * 1024
img = load(args.image, size)
ccfg = img[size - 0x58:]
bl = int.from_bytes(ccfg[0x30:0x34], "little")  # CCFG_O_BL_CONFIG
bootloader_enable, level, pin, backdoor = bl >> 24, (bl >> 16) & 1, (bl >> 8) & 0xFF, bl & 0xFF

print(f"image:  {args.image}")
print(f"BL_CONFIG = 0x{bl:08x}")
print(f"  BOOTLOADER_ENABLE = 0x{bootloader_enable:02x}  ({'ok' if bootloader_enable == 0xC5 else 'DISABLED'})")
print(f"  backdoor          = 0x{backdoor:02x}  ({'ok' if backdoor == 0xC5 else 'DISABLED'})")
print(f"  pin               = DIO{pin}  ({'ok' if pin == args.pin else f'expected DIO{args.pin}'})")
print(f"  level             = {'high' if level else 'low'} active")
reset = int.from_bytes(img[4:8], "little")
print(f"reset vector = 0x{reset:08x}  ({'ok' if 0 < reset < size else 'SUSPICIOUS'})")

ok = bootloader_enable == 0xC5 and backdoor == 0xC5 and pin == args.pin and 0 < reset < size
print("RESULT:", "SAFE to flash (bootloader stays reachable)" if ok else "DO NOT FLASH as is")
sys.exit(0 if ok else 1)
