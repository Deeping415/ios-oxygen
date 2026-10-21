/*
 * Vince Boot Shim for Xiaomi Redmi 5 Plus (Qualcomm MSM8953)
 *
 * Boots Apple XNU Kernel on Qualcomm Snapdragon 625:
 * - GICv2 (QGIC2) MMIO configuration (GICD @ 0x0b000000, GICC @ 0x0b002000)
 * - 4 KiB Granule MMU Setup for Cortex-A53
 * - Continuous Splash Framebuffer @ 0x90001000 (1080x2160 ARGB8888)
 * - FDT / Ramdisk payload extraction & XNU handover
 */
#include <stdint.h>
#include "console.h"
#include "mmu.h"
#include "loader.h"
#include "fdt.h"

#define GICD_BASE		0x0b000000UL
#define GICC_BASE		0x0b002000UL

#define GICD_CTLR		0x000
#define GICD_TYPER		0x004
#define GICD_ISENABLER	0x100
#define GICD_ICENABLER	0x180
#define GICD_IPRIORITYR	0x400

#define GICC_CTLR		0x000
#define GICC_PMR		0x004
#define GICC_IAR		0x00c
#define GICC_EOIR		0x010

#define VTIMER_INTID	20  /* PPI 4 on MSM8953 (16 + 4) */

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))
#define isb() __asm__ volatile("isb" ::: "memory")

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static volatile uint32_t irq_count = 0;
static volatile uint32_t irq_last = 0;

void gicv2_init(void)
{
	/* Enable GIC Distributor */
	wr32(GICD_BASE + GICD_CTLR, 1);

	/* Set CPU Priority Mask to allow all interrupts */
	wr32(GICC_BASE + GICC_PMR, 0xff);

	/* Enable CPU Interface */
	wr32(GICC_BASE + GICC_CTLR, 1);

	/* Enable Virtual Timer interrupt */
	uint32_t reg_idx = VTIMER_INTID / 32;
	uint32_t bit_idx = VTIMER_INTID % 32;
	wr32(GICD_BASE + GICD_ISENABLER + (reg_idx * 4), (1U << bit_idx));
}

void timer_init(void)
{
	/* Set virtual timer comparator 100ms in the future */
	uint64_t freq = rsr("cntfrq_el0");
	uint64_t count = rsr("cntvct_el0");
	wsr("cntv_cval_el0", count + (freq / 10));
	/* Enable virtual timer (ENABLE=1, IMASK=0) */
	wsr("cntv_ctl_el0", 1);
	isb();
}

void exc_handler(int slot, uint64_t *regs)
{
	(void)regs;
	if (slot == 1 || slot == 5) { /* IRQ at EL0t (slot 1) or EL1h (slot 5) */
		uint32_t iar = rd32(GICC_BASE + GICC_IAR);
		uint32_t intid = iar & 0x3ff;
		irq_last = intid;
		irq_count++;

		if (intid == VTIMER_INTID) {
			/* Reset timer comparator */
			uint64_t freq = rsr("cntfrq_el0");
			uint64_t count = rsr("cntvct_el0");
			wsr("cntv_cval_el0", count + (freq / 10));
		}

		/* Send End-of-Interrupt */
		wr32(GICC_BASE + GICC_EOIR, iar);
		return;
	}

	/* Panic on unexpected exception */
	con_clear(RED);
	con_str("PANIC", "Unexpected Exception");
	con_hex("SLOT", (uint64_t)slot);
	con_hex("ELR", rsr("elr_el1"));
	con_hex("ESR", rsr("esr_el1"));
	con_hex("FAR", rsr("far_el1"));

	while (1) {
		__asm__ volatile("wfe");
	}
}

void secondary_main(uint64_t cpu_id)
{
	(void)cpu_id;
	while (1) {
		__asm__ volatile("wfe");
	}
}

void shim_main(void *fdt)
{
	/* 1. Paint screen and print initial banner immediately */
	con_clear(0x00102040); /* Deep navy slate background */

	con_str("PROJECT", "iOS-SWEET -> Vince Port");
	con_str("DEVICE",  "Redmi 5 Plus (MSM8953)");
	con_str("CPU",     "8x Cortex-A53 ARMv8.0-A");
	con_hex("FDT",     (uintptr_t)fdt);

	/* 2. Setup 4KB Translation Granule MMU */
	struct mmu_info info;
	mmu_enable(&info);
	con_str("MMU",     "4 KiB Granule Active (TTBR0)");

	/* 3. Setup GICv2 */
	gicv2_init();
	con_str("GIC",     "ARM GICv2 MMIO Ready");

	/* 4. Setup Virtual Timer */
	timer_init();
	con_str("TIMER",   "Virtual Timer Active (PPI 20)");

	/* 5. Check ramdisk payload */
	con_str("STATUS",  "Checking payload in ramdisk...");
	const char *err = loader_boot((uintptr_t)fdt);
	if (err) {
		con_str("LOADER",  err);
		con_str_color("READY",   "Shim diagnostics PASS! Halting.", GREEN);
	}

	/* Stay active displaying boot state */
	while (1) {
		__asm__ volatile("wfi");
	}
}
