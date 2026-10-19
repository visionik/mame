// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_IO_H
#define MAME_ATT_UNIXPC_IO_H

#pragma once

#include <cstdint>

// CPU-visible windows. Zone A (0x400000) repeats every 64 KB. The boot ROM
// is the 16 KB at 0x800000 only. Expansion I/O is eight 256 KB slots from
// 0xC00000; the main chassis wires the first three.

namespace unixpc {

inline constexpr uint32_t RAM_WINDOW      = 0x000000;
inline constexpr uint32_t RAM_END         = 0x3fffff;
inline constexpr uint32_t MAPRAM_BASE     = 0x400000;
inline constexpr uint32_t MAPRAM_SIZE     = 0x800;
inline constexpr uint32_t MAPRAM_MIRROR   = 0x00f800;
inline constexpr uint32_t ZONE_WORD_MIRROR = 0x00fffe;
inline constexpr uint32_t VRAM_SIZE       = 0x8000;
inline constexpr uint32_t LATCH_STRIDE    = 0x7000;
inline constexpr uint16_t HI_BYTE         = 0xff00;
inline constexpr uint16_t LO_BYTE         = 0x00ff;
inline constexpr uint32_t GSR_BASE        = 0x410000;
inline constexpr uint32_t VRAM_BASE       = 0x420000;
inline constexpr uint32_t BSR0_BASE       = 0x430000;
inline constexpr uint32_t BSR1_BASE       = 0x440000;
inline constexpr uint32_t TSR_BASE        = 0x450000;
inline constexpr uint32_t DMA_COUNT_BASE  = 0x460000;
inline constexpr uint32_t LPSR_BASE       = 0x470000;
inline constexpr uint32_t RTC_WRITE_BASE  = 0x480000;
inline constexpr uint32_t TCR_BASE        = 0x490000;
inline constexpr uint32_t MCR_BASE        = 0x4a0000;
inline constexpr uint32_t DIALER_BASE     = 0x4b0000;
inline constexpr uint32_t DIALER_SIZE     = 0x1000;
inline constexpr uint32_t CSR_BASE        = 0x4c0000;
inline constexpr uint32_t DMA_PTR_BASE    = 0x4d0000;
inline constexpr uint32_t DMA_PTR_SIZE    = 0x8000;
inline constexpr uint32_t DISK_CTRL_BASE  = 0x4e0001;
inline constexpr uint32_t PRINTER_BASE    = 0x4f0001;
inline constexpr uint32_t PROM_BASE       = 0x800000;
inline constexpr uint32_t PROM_SIZE       = 0x004000;
inline constexpr unsigned PROM_MIRROR_BYTES = 0x2000;
inline constexpr uint32_t ROM_CHIP_SIZE   = 0x002000;
inline constexpr uint32_t EXP_BASE        = 0xc00000;
inline constexpr uint32_t EXP_SLOT_SIZE   = 0x040000;
inline constexpr int      EXP_SLOT_COUNT  = 3;
inline constexpr uint32_t HDC_BASE        = 0xe00000;
inline constexpr uint32_t HDC_REGS        = 0x10;
inline constexpr uint32_t HDC_BLOCK_MIRROR = 0x0fff0;
inline constexpr uint32_t FDC_BASE        = 0xe10000;
inline constexpr uint32_t FDC_REGS        = 8;
inline constexpr uint32_t FDC_BLOCK_MIRROR = 0x0fff8;
inline constexpr uint32_t MCR2_BASE       = 0xe20000;
inline constexpr uint32_t MCR2_ALIAS      = 0xf20000;
inline constexpr uint32_t RTC_READ_BASE   = 0xe30000;
inline constexpr uint32_t GCR_BASE        = 0xe40000;
inline constexpr uint32_t MPSC_BASE       = 0xe50000;
inline constexpr uint32_t MPSC_REGS       = 8;
inline constexpr uint32_t MODEM_BASE      = 0xe60000;
inline constexpr uint32_t MODEM_SIZE      = 0x10000;
inline constexpr uint32_t MODEM_RR2       = 0x2000;
inline constexpr uint16_t MODEM_ABSENT    = 0x00ff;
inline constexpr uint16_t IO_OPEN         = 0xffff;
inline constexpr uint32_t KBC_BASE        = 0xe70000;
inline constexpr uint32_t KBC_REGS        = 4;

// Clocks. The 68010 runs at 10 MHz (40 MHz / 4). The dot clock is 20 MHz.
inline constexpr unsigned CPU_CLK_DIV  = 4;
inline constexpr unsigned DOT_CLK_DIV  = 2;
inline constexpr unsigned FDC_CLK_DIV  = 40;
inline constexpr unsigned HDC_CLK_DIV  = 8;
inline constexpr unsigned MPSC_CLK_DIV = 8;

// Board status bits.
inline constexpr uint16_t GSR_PIE_BITS = 0x8400;
// LPINT+ is readable in GSR. FreeBee documents bit 9 (0x0200). Diagnostic
// Disk Ver 3.51's level-2 ISR discriminates the printer with
// `btst #3,($410000)` — bit 3 of the high byte = GSR bit 11 (0x0800) —
// then sets the LPFint software flag and clears the flip-flop via a
// printer-data write. Expose both bits so the ISR and FreeBee agree.
inline constexpr uint16_t GSR_LPINT = 0x0a00;
inline constexpr uint16_t TSR_IDLE     = 0x000f;
inline constexpr uint16_t TSR_P51      = 0x1010;
// MCR2 D0 is HDSEL3 (P5.1 PAL only). D1 is the second-drive select
// UNIX already honors; the ICUS board and Milton's board both decode
// it. Milton's D2 and D3 were further drive selects with no software.
// D4 is the P5.1 identity latch, mirrored on TSR when that PAL is fitted.
inline constexpr int MCR2_HDSEL3_BIT   = 0;
inline constexpr int MCR2_DDRIVE1_BIT  = 1;
inline constexpr int MCR2_DDRIVE2_BIT  = 2;
inline constexpr int MCR2_DDRIVE3_BIT  = 3;
inline constexpr int MCR2_P51_BIT      = 4;
inline constexpr int LATCH_ADDR_SHIFT  = 11;
inline constexpr int TCR_DATA_BIT      = 14;
inline constexpr int TCR_ADDR_SHIFT    = LATCH_ADDR_SHIFT;
inline constexpr int GCR_DATA_BIT      = 15;
inline constexpr int LPSR_NO_DIALTONE  = 0;
inline constexpr int LPSR_NO_PERR      = 1;
inline constexpr int LPSR_HDC_IRQ      = 2;
inline constexpr int LPSR_FDC_IRQ      = 3;
// Printer inputs. 1 is busy, selected, or out of paper. Fault is the
// other way: 1 means no error. Acknowledge is a pulse, not a bit here.
inline constexpr int LPSR_FAULT        = 4;
inline constexpr int LPSR_PAPER        = 5;
inline constexpr int LPSR_SELECT       = 6;
inline constexpr int LPSR_BUSY         = 7;
inline constexpr uint16_t LPSR_PRINTER_FLOAT =
	(1u << LPSR_FAULT) | (1u << LPSR_PAPER) | (1u << LPSR_SELECT) | (1u << LPSR_BUSY);

// fault_ok is 1 when the fault pin is not reporting an error.
inline constexpr uint16_t lpsr_printer_bits(bool busy, bool selected, bool paper_out, bool fault_ok)
{
	uint16_t bits = 0;
	if (busy)
		bits |= 1u << LPSR_BUSY;
	if (selected)
		bits |= 1u << LPSR_SELECT;
	if (paper_out)
		bits |= 1u << LPSR_PAPER;
	if (fault_ok)
		bits |= 1u << LPSR_FAULT;
	return bits;
}

// Acknowledge is active-low. The leading edge accepts the byte.
// The trailing edge is the return to high, and that clocks LPINT+.
inline constexpr bool lp_ack_leading(bool was_high, bool now_high)
{
	return was_high && !now_high;
}

inline constexpr bool lp_ack_trailing(bool was_high, bool now_high)
{
	return !was_high && now_high;
}

// Busy stays set from the leading edge until the delayed trailing edge.
inline constexpr bool lp_visible_busy(bool pin_busy, bool holding)
{
	return pin_busy || holding;
}

// A real printer holds busy for milliseconds. The generic printer
// finishes the whole handshake in about 20 us. Stretch visible busy
// this long, park STROBE* idle on the device for the same window, and
// raise LPINT+ when the stretch ends so not-busy and the interrupt
// arrive together (Transfer Test keys the next byte off that IRQ).
// LPINT+ stays latched until a printer-data write.
inline constexpr int LP_ACK_DELAY_USEC = 2000;

} // namespace unixpc

#endif
