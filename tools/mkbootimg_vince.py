#!/usr/bin/env python3
"""Pack an Android boot image (header v0) for Xiaomi Redmi 5 Plus (vince)."""
import argparse
import hashlib
import os
import struct
import sys

def round_up(n, page_size):
    return (n + page_size - 1) // page_size * page_size

def pack_bootimg(kernel_path, ramdisk_path, out_path, dtb_path=None, cmdline="console=null loop.max_part=7", page_size=2048):
    with open(kernel_path, "rb") as f:
        kernel = f.read()
    
    if ramdisk_path and os.path.exists(ramdisk_path):
        with open(ramdisk_path, "rb") as f:
            ramdisk = f.read()
    else:
        ramdisk = b""

    dtb = b""
    if dtb_path and os.path.exists(dtb_path):
        with open(dtb_path, "rb") as f:
            dtb = f.read()

    # Offsets for Vince (MSM8953 / Snapdragon 625)
    base = 0x80000000
    kernel_addr = base + 0x00008000
    ramdisk_addr = base + 0x01000000
    tags_addr = base + 0x00000100
    second_addr = base + 0x00f00000

    # Header v0 structure (1632 bytes)
    magic = b"ANDROID!"
    kernel_size = len(kernel)
    ramdisk_size = len(ramdisk)
    second_size = len(dtb) if dtb else 0
    header_version = 0
    os_version = (8 << 25) | (1 << 22)  # Android 8.1 stand-in

    cmdline_bytes = cmdline.encode("utf-8")[:512].ljust(512, b"\0")
    name_bytes = b"vince-xnu".ljust(16, b"\0")

    # SHA hash over kernel, ramdisk, second
    sha = hashlib.sha1()
    sha.update(kernel)
    sha.update(struct.pack("<I", kernel_size))
    sha.update(ramdisk)
    sha.update(struct.pack("<I", ramdisk_size))
    sha.update(dtb if dtb else b"")
    sha.update(struct.pack("<I", second_size))
    id_hash = sha.digest().ljust(32, b"\0")

    header = struct.pack(
        "<8s10I16s512s32s1024s",
        magic,
        kernel_size,
        kernel_addr,
        ramdisk_size,
        ramdisk_addr,
        second_size,
        second_addr,
        tags_addr,
        page_size,
        header_version,
        os_version,
        name_bytes,
        cmdline_bytes,
        id_hash,
        b"\0" * 1024  # extra cmdline
    )

    with open(out_path, "wb") as f:
        # Write header padded to page_size
        f.write(header.ljust(page_size, b"\0"))
        
        # Write kernel padded to page_size
        if kernel_size > 0:
            f.write(kernel.ljust(round_up(kernel_size, page_size), b"\0"))
            
        # Write ramdisk padded to page_size
        if ramdisk_size > 0:
            f.write(ramdisk.ljust(round_up(ramdisk_size, page_size), b"\0"))

        # Write dtb / second if present
        if second_size > 0:
            f.write(dtb.ljust(round_up(second_size, page_size), b"\0"))

    total_size = os.path.getsize(out_path)
    sha256 = hashlib.sha256(open(out_path, "rb").read()).hexdigest()
    print(f"[+] Boot image packed: {out_path} ({total_size} bytes)")
    print(f"    Kernel: {kernel_size} B, Ramdisk: {ramdisk_size} B, Pagesize: {page_size}")
    print(f"    SHA256: {sha256}")
    return sha256

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Pack Vince Android boot.img")
    parser.add_argument("--kernel", required=True, help="Kernel binary")
    parser.add_argument("--ramdisk", default=None, help="Ramdisk / payload binary")
    parser.add_argument("--dtb", default=None, help="DTB binary")
    parser.add_argument("--out", required=True, help="Output boot.img")
    parser.add_argument("--cmdline", default="console=null loop.max_part=7", help="Kernel command line")
    args = parser.parse_args()

    pack_bootimg(args.kernel, args.ramdisk, args.out, args.dtb, args.cmdline)
