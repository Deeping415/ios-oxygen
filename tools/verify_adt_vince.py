#!/usr/bin/env python3
"""Automated verification script for Vince ADT against SWEET regressions."""
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "upstream", "ios-sweet", "tools"))
import adt

SWEET_TRAPS = {
    "sweet_gicd": 0x17a00000,
    "sweet_fb": 0x9c000000,
    "sweet_ufs": 0x01d84000,
    "sweet_height": 2400,
}

def verify_adt(adt_path):
    print(f"[*] Verifying Vince ADT: {adt_path}")
    assert os.path.exists(adt_path), f"File not found: {adt_path}"
    
    with open(adt_path, "rb") as f:
        data = f.read()
    
    root, _ = adt.parse(data)
    props = {k.decode() if isinstance(k, bytes) else k: v for k, _, v in root.props}
    
    # Check 1: Target type must be vince
    target_type = props.get("target-type", b"").rstrip(b"\0").decode()
    assert target_type == "vince", f"Target type mismatch: {target_type} != vince"
    print("  [PASS] Target type is 'vince'")

    # Walk children
    arm_io = None
    chosen = None
    for c in root.children:
        c_name = dict((k.rstrip(b'\0').decode() if isinstance(k, bytes) else k, v) for k, _, v in c.props).get("name", b"").rstrip(b'\0').decode()
        if c_name == "arm-io":
            arm_io = c
        elif c_name == "chosen":
            chosen = c

    assert arm_io is not None, "arm-io node missing"
    assert chosen is not None, "chosen node missing"

    # Check 2: Reserved RAM carveout overlap
    chosen_props = dict((k.rstrip(b'\0').decode() if isinstance(k, bytes) else k, v) for k, _, v in chosen.props)
    import struct
    dram_base, = struct.unpack("<Q", chosen_props["dram-base"])
    dram_size, = struct.unpack("<Q", chosen_props["dram-size"])
    print(f"  [PASS] DRAM Base: 0x{dram_base:x}, Size: 0x{dram_size:x}")
    if dram_base < 0x100000000:
        assert dram_base >= 0x92100000, f"DRAM base 0x{dram_base:x} overlaps Qualcomm modem/TZ carveouts!"
        print("  [PASS] DRAM base is safely above all hardware carve-outs")

    # Check 3: Check arm-io peripherals
    for dev in arm_io.children:
        dev_props = dict((k.rstrip(b'\0').decode() if isinstance(k, bytes) else k, v) for k, _, v in dev.props)
        name = dev_props.get("name", b"").rstrip(b"\0").decode()
        
        if name == "clcd":
            w, = struct.unpack("<I", dev_props["width"])
            h, = struct.unpack("<I", dev_props["height"])
            fb_reg = struct.unpack("<2Q", dev_props["reg"])
            assert fb_reg[0] != SWEET_TRAPS["sweet_fb"], "REGRESSION: clcd uses SWEET FB 0x9c000000!"
            assert fb_reg[0] == 0x90001000, f"clcd reg mismatch: 0x{fb_reg[0]:x} != 0x90001000"
            assert w == 1080 and h == 2160, f"clcd dimensions {w}x{h} != 1080x2160"
            print(f"  [PASS] CLCD Framebuffer verified: 0x{fb_reg[0]:x} ({w}x{h})")

        elif name == "interrupt-controller":
            gic_reg = struct.unpack("<4Q", dev_props["reg"])
            assert gic_reg[0] != SWEET_TRAPS["sweet_gicd"], "REGRESSION: GICD uses SWEET GICv3 0x17a00000!"
            assert gic_reg[0] == 0x0b000000, f"GICD reg mismatch: 0x{gic_reg[0]:x} != 0x0b000000"
            assert gic_reg[2] == 0x0b002000, f"GICC reg mismatch: 0x{gic_reg[2]:x} != 0x0b002000"
            print(f"  [PASS] GICv2 verified: GICD=0x{gic_reg[0]:x}, GICC=0x{gic_reg[2]:x}")

        elif name == "sdhci":
            sdhci_reg = struct.unpack("<2Q", dev_props["reg"])
            assert sdhci_reg[0] != SWEET_TRAPS["sweet_ufs"], "REGRESSION: Storage uses SWEET UFS 0x01d84000!"
            assert sdhci_reg[0] == 0x07824900, f"SDHCI reg mismatch: 0x{sdhci_reg[0]:x} != 0x07824900"
            print(f"  [PASS] SDHCI eMMC controller verified: 0x{sdhci_reg[0]:x}")

        elif name == "touchscreen":
            i2c_addr, = struct.unpack("<I", dev_props["i2c-address"])
            i2c_bus, = struct.unpack("<I", dev_props["i2c-bus"])
            assert i2c_addr == 0x20 and i2c_bus == 3, f"Touchscreen mismatch: bus {i2c_bus}, addr 0x{i2c_addr:x}"
            print(f"  [PASS] Synaptics RMI4 Touchscreen verified: Bus {i2c_bus}, Addr 0x{i2c_addr:x}")

    print("[SUCCESS] All ADT verification checks passed with ZERO SWEET regressions!")
    return True

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "artifacts/vince.adt"
    verify_adt(path)
