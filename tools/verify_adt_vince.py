#!/usr/bin/env python3
"""Verification suite for Vince Apple Device Tree (ADT)."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt

def verify(adt_path):
    data = open(adt_path, "rb").read()
    root, _ = adt.parse(data)
    assert root.prop("target-type") == b"vince\0", "Target type must be vince"
    chosen = next(c for c in root.children if c.name == "chosen")
    dram_base, = struct.unpack("<Q", chosen.prop("dram-base"))
    assert dram_base == 0xa0000000, "DRAM base must be 0xa0000000"
    arm_io = next(c for c in root.children if c.name == "arm-io")
    gic = next(c for c in arm_io.children if c.name == "interrupt-controller")
    reg = gic.prop("reg")
    gicd, _, gicc, _ = struct.unpack("<4Q", reg)
    assert gicd == 0x0b000000 and gicc == 0x0b002000, "GICv2 MMIO addresses match"
    print("[PASS] All Vince ADT verification checks passed!")

if __name__ == "__main__":
    verify(sys.argv[1] if len(sys.argv) > 1 else "artifacts/vince.adt")
