# ios-vince

Apple XNU-8796 kernel port and Vince Boot Shim for Xiaomi Redmi 5 Plus (vince / Qualcomm MSM8953 Snapdragon 625), targeting iOS 16 userland.

## Target Hardware
- **SoC:** Qualcomm Snapdragon 625 (MSM8953), 8x ARM Cortex-A53 @ 2.0 GHz
- **Display:** 1080x2160 FHD+ 24-bit RGB888, stride 3240 bytes (Novatek/Synaptics TD4310)
- **Interrupt Controller:** ARM GICv2 MMIO (Distributor: 0x0b000000, CPU Interface: 0x0b002000)
- **Timer:** ARM Generic Virtual Timer (PPI 20 / INTID 20)
- **Touch:** Synaptics RMI4 I2C (0x20 on I2C-3)
- **Storage:** Qualcomm SDHCI eMMC 5.1 (0x07824900)
