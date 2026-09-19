// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_DMA_H
#define MAME_ATT_UNIXPC_DMA_H

#pragma once

#include <cstdint>
#include <algorithm>

// UNIX PC disk DMA: 14-bit word count, 24-bit pointer, direction, enable.
// The hardware transfers 16-bit words while the selected controller asserts
// DRQ. A per-slice budget keeps a full sector from monopolising the CPU.

namespace unixpc {

enum class dma_source : uint8_t
{
	none = 0,
	floppy,
	hard_disk
};

class dma_engine
{
public:
	static constexpr uint16_t COUNT_MASK = 0x3fff;
	static constexpr int DEFAULT_BUDGET = 20; // 1 MHz DMA, ~20 µs slice

	void reset()
	{
		m_pointer = 0;
		m_count = 0;
		m_reading = false;
		m_enabled = false;
		m_source = dma_source::none;
	}

	void set_count(uint16_t count) { m_count = uint16_t(count & COUNT_MASK); }
	uint16_t count() const { return m_count; }

	void set_pointer_low(uint32_t bits)
	{
		m_pointer = (m_pointer & 0x3ffe00u) | (bits & 0x1feu);
	}

	void set_pointer_high(uint32_t bits)
	{
		m_pointer = (m_pointer & 0x1feu) | ((bits & 0x3ffeu) << 8);
	}

	uint32_t pointer() const { return m_pointer; }

	void set_reading(bool from_memory) { m_reading = from_memory; }
	bool reading() const { return m_reading; }

	void set_enabled(bool on) { m_enabled = on; }
	bool enabled() const { return m_enabled; }

	void set_source(dma_source s) { m_source = s; }
	dma_source source() const { return m_source; }

	bool finished() const { return m_count >= COUNT_MASK; }

	bool wants_transfer() const
	{
		return m_enabled && m_source != dma_source::none && !finished();
	}

	// DMA miss: controller has DRQ but the engine is not taking data.
	bool miss(bool device_drq) const
	{
		return device_drq && !wants_transfer();
	}

	int service(bool device_drq, int budget = DEFAULT_BUDGET)
	{
		if (!wants_transfer() || !device_drq || budget <= 0)
			return 0;

		int const n = std::min(budget, int(COUNT_MASK - m_count));
		return n;
	}

	void advance()
	{
		m_pointer = (m_pointer + 2) & 0x3ffffeu;
		if (m_count < COUNT_MASK)
			m_count++;
	}

	void complete_if_done()
	{
		if (m_count >= COUNT_MASK)
			m_count = COUNT_MASK;
	}

private:
	uint32_t m_pointer = 0;
	uint16_t m_count = 0;
	bool m_reading = false;
	bool m_enabled = false;
	dma_source m_source = dma_source::none;
};

} // namespace unixpc

#endif // MAME_ATT_UNIXPC_DMA_H
