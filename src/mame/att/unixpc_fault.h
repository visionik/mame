// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_FAULT_H
#define MAME_ATT_UNIXPC_FAULT_H

#pragma once

#include "unixpc_mmu.h"

#include <cstdint>

// General Status / Bus Status encoding for the UNIX PC.
//
// CPU faults raise a 68010 bus error when Error Enable is set.
// DMA faults raise a level-7 NMI, latched until Clear Status is written.
// MCR bit 15 is CLRSINT- (active-low clear of the 60 Hz IRQ6 latch), not a
// timer enable.

namespace unixpc {

enum class fault_kind : uint8_t
{
	page_fault,
	privileged,
	kernel_or_protect
};

struct status_regs
{
	uint16_t genstat = 0;
	uint16_t bsr0 = 0;
	uint16_t bsr1 = 0;
};

inline fault_kind kind_from(access a)
{
	switch (a)
	{
	case access::page_fault: return fault_kind::page_fault;
	case access::privileged: return fault_kind::privileged;
	default:                 return fault_kind::kernel_or_protect;
	}
}

inline uint16_t pie_bits(bool pie)
{
	return pie ? uint16_t(0x8400) : uint16_t(0);
}

inline uint16_t cpu_genstat(fault_kind kind, bool write, bool pie)
{
	uint16_t g = pie_bits(pie);
	if (!write)
		g |= 0x4000;

	switch (kind)
	{
	case fault_kind::page_fault:        g |= 0x01ff; break;
	case fault_kind::privileged:        g |= 0x10ff; break;
	case fault_kind::kernel_or_protect: g |= 0x11ff; break;
	}
	return g;
}

inline uint16_t dma_genstat(bool dma_from_memory, bool pie)
{
	uint16_t g = uint16_t(0x21ff) | pie_bits(pie);
	if (dma_from_memory)
		g |= 0x4000;
	return g;
}

inline uint16_t cpu_bsr0(uint32_t addr, int width_bits, bool privileged)
{
	uint16_t b;
	if (width_bits >= 16)
		b = 0x7c00;
	else
		b = (addr & 1) ? uint16_t(0x7e00) : uint16_t(0x7d00);
	if (privileged)
		b |= 0x8000;
	b |= uint16_t((addr >> 16) & 0xff);
	return b;
}

inline uint16_t dma_bsr0(uint32_t addr)
{
	return uint16_t(0x3c00 | ((addr >> 16) & 0xff));
}

inline status_regs encode_cpu_fault(fault_kind kind, bool write, uint32_t addr, int width_bits, bool pie)
{
	status_regs r;
	r.genstat = cpu_genstat(kind, write, pie);
	r.bsr0 = cpu_bsr0(addr, width_bits, kind == fault_kind::privileged);
	r.bsr1 = uint16_t(addr);
	return r;
}

inline status_regs encode_dma_fault(bool dma_from_memory, uint32_t addr, bool pie)
{
	status_regs r;
	r.genstat = dma_genstat(dma_from_memory, pie);
	r.bsr0 = dma_bsr0(addr);
	r.bsr1 = uint16_t(addr);
	return r;
}

class board_status
{
public:
	void reset()
	{
		m_regs = {};
		m_ee = false;
		m_pie = false;
		m_nmi_latched = false;
		m_timer_latched = false;
		m_clrsint_high = false; // MCR reads 0 after reset: CLRSINT- asserted
		m_status_frozen = false;
	}

	void set_ee(bool v) { m_ee = v; }
	void set_pie(bool v) { m_pie = v; }
	bool ee() const { return m_ee; }
	bool pie() const { return m_pie; }

	status_regs const &regs() const { return m_regs; }

	// GSR/BSR stay frozen until Clear Status. A second miss must not
	// overwrite the latched cycle or the handler pages in the wrong VA.
	bool latch_cpu(status_regs const &r)
	{
		if (m_status_frozen)
			return false;
		m_regs = r;
		m_status_frozen = true;
		return true;
	}

	bool status_frozen() const { return m_status_frozen; }

	// Returns true if the CPU NMI line should be asserted (once per fault).
	bool latch_dma(status_regs const &r)
	{
		m_regs = r;
		if (!m_ee || m_nmi_latched)
			return false;
		m_nmi_latched = true;
		return true;
	}

	void clear_status()
	{
		m_regs.bsr0 |= 0x8000;
		m_regs.genstat |= 0x1100;
		m_nmi_latched = false;
		m_status_frozen = false;
	}

	bool nmi_latched() const { return m_nmi_latched; }

	// CLRSINT- is active low. Passing false (bit 15 clear) holds the 60 Hz
	// latch cleared. Passing true allows the next vblank to set it.
	void set_clrsint_high(bool high)
	{
		m_clrsint_high = high;
		if (!high)
			m_timer_latched = false;
	}

	bool clrsint_high() const { return m_clrsint_high; }

	void vblank()
	{
		if (m_clrsint_high)
			m_timer_latched = true;
	}

	bool timer_latched() const { return m_timer_latched; }

private:
	status_regs m_regs{};
	bool m_ee = false;
	bool m_pie = false;
	bool m_nmi_latched = false;
	bool m_timer_latched = false;
	bool m_clrsint_high = false;
	bool m_status_frozen = false;
};

} // namespace unixpc

#endif // MAME_ATT_UNIXPC_FAULT_H
