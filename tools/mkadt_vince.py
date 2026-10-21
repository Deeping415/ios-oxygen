#!/usr/bin/env python3
"""Build the Apple Device Tree (ADT) for Xiaomi Redmi 5 Plus (vince) Qualcomm MSM8953."""
import hashlib, os, struct, sys, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adt

PHYS_BASE = 0xa0000000
MEM_SIZE = 0x58000000
PANIC_LOG_SIZE = 0x100000

GICD, GICD_SIZE = 0x0b000000, 0x1000
GICC, GICC_SIZE = 0x0b002000, 0x1000

FB, FB_SIZE = 0x90001000, 1080 * 2160 * 3
FB_W, FB_H, FB_STRIDE = 1080, 2160, 1080 * 3

SDHCI, SDHCI_SIZE = 0x07824900, 0x1000
TIMEBASE = 19200000
VTIMER_INTID = 20

VINCE_ECID = 0x56494e4345000001
BOOT_MANIFEST_HASH = hashlib.sha384(b'vince boot manifest').digest()

u32 = lambda *v: struct.pack('<%dI' % len(v), *v)
u64 = lambda *v: struct.pack('<%dQ' % len(v), *v)
cstr = lambda s: s.encode() + b'\0'

def node(name, props=(), children=()):
    n = adt.Node()
    n.props = [('name', 0, cstr(name))] + [(k, 0, v) for k, v in props]
    n.children = list(children)
    return n

def build_vince_adt(seed=None):
    phys_base, mem_size = PHYS_BASE, MEM_SIZE
    mem_size -= PANIC_LOG_SIZE
    seed = seed if seed is not None else os.urandom(64)
    phandle = iter(range(1, 1000))
    ph = lambda: ('AAPL,phandle', u32(next(phandle)))

    chosen = node('chosen', [
        ph(),
        ('dram-base', u64(phys_base)),
        ('dram-size', u64(mem_size)),
        ('random-seed', seed),
        ('debug-enabled', u32(1)),
        ('display-scale', u32(3)),
        ('display-rotation', u32(0)),
        ('unique-chip-id', u64(VINCE_ECID)),
        ('chip-id', u32(0x8015)),
        ('board-id', u32(0x16)),
        ('embedded-panic-log-size', u32(PANIC_LOG_SIZE)),
        ('firmware-version', cstr('vince-shim')),
        ('system-firmware-version', cstr('vince-shim')),
        ('boot-manifest-hash', BOOT_MANIFEST_HASH),
    ])

    gic = node('interrupt-controller', [
        ph(),
        ('compatible', cstr('arm,gic-400') + cstr('arm,cortex-a15-gic') + b'\0'),
        ('reg', u64(GICD, GICD_SIZE, GICC, GICC_SIZE)),
        ('interrupt-controller', b''),
        ('#interrupt-cells', u32(3)),
        ('virtual-timer-intid', u32(VTIMER_INTID)),
    ])

    wdt = node('wdt', [ph(), ('no-pmu', u32(1))])

    disp0 = node('disp0', [
        ph(),
        ('device_type', cstr('display-subsystem')),
        ('compatible', cstr('vince,display') + cstr('sweet,display') + b'\0'),
        ('canvas-width', u32(FB_W)),
        ('canvas-height', u32(FB_H)),
    ])

    sdhci_node = node('sdhci', [
        ph(),
        ('compatible', cstr('generic-sdhci') + cstr('qcom,sdhci-msm-v4') + b'\0'),
        ('reg', u64(SDHCI, SDHCI_SIZE)),
        ('interrupts', u32(0, 123, 4)),
    ])

    touch_node = node('touchscreen', [
        ph(),
        ('compatible', cstr('synaptics,rmi4') + b'\0'),
        ('i2c-address', u32(0x20)),
        ('i2c-bus', u32(3)),
        ('interrupts', u32(0, 65, 2)),
    ])

    arm_io = node('arm-io', [ph(), ('ranges', b'')], [gic, wdt, disp0, sdhci_node, touch_node])

    fbcon = node('vince-fbcon', [
        ph(),
        ('reg', u64(FB, FB_SIZE)),
        ('width', u32(FB_W)),
        ('height', u32(FB_H)),
        ('stride', u32(FB_STRIDE)),
    ])

    defaults = node('defaults', [ph(), ('no-effaceable-storage', u32(1))])
    buttons = node('buttons', [ph(), ('home-button-type', u32(2))])

    cpus = [node(f'cpu{i}', [
        ph(),
        ('device_type', cstr('cpu')),
        ('reg', u32(i)),
        ('timebase-frequency', u32(TIMEBASE)),
        ('clock-frequency', u32(2016000000)),
        ('cpu-model', cstr('cortex-a53')),
    ]) for i in range(8)]

    product = node('product', [
        ph(),
        ('product-id', u32(0x1003)),
        ('product-name', cstr('iPhone X')),
        ('model-number', cstr('D22AP')),
        ('device-class', cstr('iPhone')),
    ])

    return node('', [
        ('model', cstr('vince')),
        ('compatible', cstr('xiaomi,vince') + cstr('qcom,msm8953') + b'\0'),
        ('target-type', cstr('vince')),
        ('device-id', u64(VINCE_ECID)),
        ('timebase-frequency', u32(TIMEBASE)),
    ], [chosen, defaults, node('cpus', [ph()], cpus), arm_io, fbcon, buttons, product])

def main():
    parser = argparse.ArgumentParser(description="Generate Vince Apple Device Tree")
    parser.add_argument("output", help="Output .adt file path")
    args = parser.parse_args()
    data = adt.serialize(build_vince_adt())
    with open(args.output, "wb") as f:
        f.write(data)
    print(f"[+] Vince ADT successfully generated: {args.output} ({len(data)} bytes)")

if __name__ == "__main__":
    main()
