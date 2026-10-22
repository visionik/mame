// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_ENET_H
#define MAME_ATT_UNIXPC_ENET_H

#pragma once

#include <cstdint>

// AT&T UNIX PC Ethernet expansion board, from the theory-of-operation
// manual. Offsets are bytes inside the slot's 256 KiB window.
//
// The LANCE data and address ports are at the bottom of the window.
// A read of either port does not return the chip's data in that cycle:
// the value is captured into the data latch, and the next latch read
// delivers it. A write of the status-ring register clears a pending
// interrupt. A read of the control register does too.
//
// The loadable driver asks for identity 0x1003 and never reads the
// identity bytes itself. A write at the bottom of that block resets
// the card. The station address is six low bytes on odd addresses,
// octet 0 at ENET_MAC0 and octet 5 at ENET_MAC_LSB.
// The status-ring store is the byte address shifted right by 9.

namespace unixpc {

inline constexpr uint32_t ENET_RDP    = 0x000000;
inline constexpr uint32_t ENET_RAP    = 0x000002;
inline constexpr uint32_t ENET_CTL    = 0x000004;
inline constexpr uint32_t ENET_LATCH  = 0x000006;
inline constexpr uint32_t ENET_MAC0 = 0x0003ffcb;
inline constexpr uint32_t ENET_MAC_LSB = 0x0003ffc1;
inline constexpr int ENET_MAC_BYTES = 6;
inline constexpr int ENET_RING_SHIFT = 9;
inline constexpr uint32_t ENET_STATUS_RING_BYTES = 0x400;
inline constexpr uint8_t ENET_CTL_BOOT = 0x05;

// CSR0 bits the board copies into the status ring. INTR is stripped —
// WIN/3B enintr treats bit 7 as a fatal snapshot. ERR is stripped too
// (combinatorial OR) so the high byte stays a single cause code.
inline constexpr uint16_t ENET_CSR0_INEA = 0x0040;
inline constexpr uint16_t ENET_CSR0_INTR = 0x0080;
inline constexpr uint16_t ENET_CSR0_ERR  = 0x8000;
inline constexpr uint16_t ENET_CSR0_MISS = 0x1000;
inline constexpr uint16_t ENET_CSR0_CLEAR = 0x7f00; // BABL|CERR|MISS|MERR|RINT|TINT|IDON

inline constexpr uint8_t ENET_DMAEN = 0x01;
inline constexpr uint8_t ENET_OTHER = 0x02;
inline constexpr uint8_t ENET_INT1  = 0x04;
inline constexpr uint8_t ENET_SPARE = 0x08;
inline constexpr uint8_t ENET_CTL_MASK = 0x0f;

// Octet 0 is at ENET_MAC0. Returns -1 if this is not a station byte.
// Same even/odd rule as exp_id_byte: the 16-bit I/O map delivers the even
// word base, and the station byte sits on the odd address of that word.
inline constexpr int enet_mac_octet(uint32_t byte_offset)
{
	uint32_t const addr = byte_offset | 1u;
	if (addr > ENET_MAC0 || addr < ENET_MAC_LSB)
		return -1;
	uint32_t const delta = ENET_MAC0 - addr;
	if ((delta & 1u) != 0)
		return -1;
	int const n = int(delta / 2u);
	if (n >= ENET_MAC_BYTES)
		return -1;
	return n;
}

inline constexpr uint32_t enet_ring_address(uint16_t stored)
{
	return uint32_t(stored) << ENET_RING_SHIFT;
}

class enet_board
{
public:
	void reset()
	{
		m_control = 0;
		m_ring = 0;
		m_ring_prod = 0;
		m_latch = 0;
		m_irq = false;
	}

	void capture(uint16_t value) { m_latch = value; }
	uint16_t latch() const { return m_latch; }

	void write_control(uint16_t data) { m_control = uint8_t(data & ENET_CTL_MASK); }

	// Reading the control register clears the board interrupt.
	uint16_t read_control()
	{
		m_irq = false;
		return m_control;
	}

	void write_ring(uint16_t data)
	{
		m_ring = data;
		m_ring_prod = 0;
		m_irq = false;
	}

	// Next absolute byte address for a status-ring CSR0 store, then advance.
	uint32_t take_status_slot()
	{
		uint32_t const addr = ring_address() + m_ring_prod;
		m_ring_prod = uint16_t((m_ring_prod + 2) & (ENET_STATUS_RING_BYTES - 1));
		return addr;
	}

	uint16_t ring() const { return m_ring; }
	uint32_t ring_address() const { return enet_ring_address(m_ring); }
	uint16_t ring_prod() const { return m_ring_prod; }
	void set_ring_prod(uint16_t prod)
	{
		m_ring_prod = uint16_t(prod & (ENET_STATUS_RING_BYTES - 1));
		if (m_ring_prod & 1)
			m_ring_prod = uint16_t(m_ring_prod & ~1u);
	}
	uint8_t control() const { return m_control; }
	bool irq() const { return m_irq; }
	void raise_irq() { m_irq = true; }
	bool dma_enabled() const { return (m_control & ENET_DMAEN) != 0; }
	bool ethernet_selected() const { return (m_control & ENET_OTHER) == 0; }
	bool interrupt_level_1() const { return (m_control & ENET_INT1) != 0; }
	bool status_ring_armed() const { return m_ring != 0 && dma_enabled(); }

	// Public for device_t::save_item; not a guest-visible API.
	uint8_t m_control = 0;
	uint16_t m_ring = 0;
	uint16_t m_ring_prod = 0;
	uint16_t m_latch = 0;
	bool m_irq = false;
};

} // namespace unixpc

#endif
