// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_SLOT_H
#define MAME_ATT_UNIXPC_SLOT_H

#pragma once

#include <cstdint>

// Eight 256 KiB expansion I/O windows, selected by the slot's ID pins.
// The chassis has three of those windows. An empty window returns 0xFF
// and a write of the identity bytes is a card reset. A card is accepted
// only when both identity bytes match and both complements sum to 0.
//
// The dual-EIA card is a Z8530 on the low data byte. Channel B control is
// the first word. The published combo driver registers its service routine
// on autovector 5, and the three chassis slots share that line. Each slot
// drives its own input on the merger so one card cannot clear another.
//
// Expansion RAM sits from 2 MiB to 4 MiB. A 512 KiB card uses the slot ID
// to pick one half-megabyte bank. A 1 MiB card uses a pair of banks.
// A 1.5 MiB EIA/RAM card and a 2 MiB RAM card ignore the slot ID and
// occupy a fixed span of that region.

namespace unixpc {

inline constexpr uint32_t EXP_IO_BASE = 0x00c00000;
inline constexpr uint32_t EXP_IO_SIZE = 0x00040000;
inline constexpr int EXP_IO_WINDOWS = 8;
inline constexpr uint32_t EXP_RAM_BASE = 0x00200000;
inline constexpr uint32_t EXP_RAM_LIMIT = 0x00400000;
inline constexpr uint32_t EXP_RAM_BANK = 0x00080000;

inline constexpr uint32_t EXP_ID_LO = 0x0003fff9;
inline constexpr uint32_t EXP_ID_HI = 0x0003fffb;
inline constexpr uint32_t EXP_ID_LO_COMP = 0x0003fffd;
inline constexpr uint32_t EXP_ID_HI_COMP = 0x0003ffff;
inline constexpr uint8_t EXP_EMPTY = 0xff;

inline constexpr uint32_t exp_window_base(int index)
{
	return EXP_IO_BASE + uint32_t(index) * EXP_IO_SIZE;
}

inline constexpr bool exp_id_address(uint32_t offset)
{
	return offset == EXP_ID_LO || offset == EXP_ID_HI
		|| offset == EXP_ID_LO_COMP || offset == EXP_ID_HI_COMP;
}

// Combo driver record 00 10 00 F0 is high 0x00, low 0x10,
// complement of high 0x00, complement of low 0xF0.
inline constexpr uint16_t EIA_ID = 0x0010;
// The combo object passes this to the kernel registrar. Level 5 is
// autovector 29, handler offset 0x74. Ethernet can also select it.
inline constexpr int EIA_IPL = 5;
inline constexpr int EXP_SLOTS = 3;

inline constexpr int cpu_autovector(int ipl)
{
	return 24 + ipl;
}

inline constexpr int cpu_autovector_offset(int ipl)
{
	return 4 * cpu_autovector(ipl);
}

// Merger input for a chassis slot, or -1 if the slot is not on this bus.
inline constexpr int exp_irq_bit(int slot)
{
	if (slot < 0 || slot >= EXP_SLOTS)
		return -1;
	return slot;
}
// The Ethernet driver asks the kernel slot lookup for this value.
inline constexpr uint16_t ENET_ID = 0x1003;

// Low byte of the identity word at this offset, or -1.
//
// Identity lives on the odd addresses. The expansion I/O handler is a
// 16-bit map entry, so it always receives the even word base (offset & ~1).
// Match either form so a word read that covers the odd byte still returns
// the id in D7-D0 (0xff00 | id), which is what the slot walk samples.
inline constexpr int exp_id_byte(uint16_t id, uint32_t offset)
{
	uint8_t const lo = uint8_t(id);
	uint8_t const hi = uint8_t(id >> 8);
	uint32_t const addr = offset | 1u;
	if (addr == EXP_ID_LO)
		return lo;
	if (addr == EXP_ID_HI)
		return hi;
	if (addr == EXP_ID_LO_COMP)
		return int(uint8_t(0u - lo));
	if (addr == EXP_ID_HI_COMP)
		return int(uint8_t(0u - hi));
	return -1;
}

// Z8530 on the EIA/RAM combo card. Channel B is the lower address.
// That channel's data word is four bytes above its control word.
inline constexpr uint32_t EIA_B_CTRL = 0x0;
inline constexpr uint32_t EIA_A_CTRL = 0x2;
inline constexpr uint32_t EIA_B_DATA = 0x4;
inline constexpr uint32_t EIA_A_DATA = 0x6;

enum class eia_reg : uint8_t { none, b_ctrl, a_ctrl, b_data, a_data };

inline constexpr eia_reg eia_decode(uint32_t offset)
{
	switch (offset)
	{
	case EIA_B_CTRL: return eia_reg::b_ctrl;
	case EIA_A_CTRL: return eia_reg::a_ctrl;
	case EIA_B_DATA: return eia_reg::b_data;
	case EIA_A_DATA: return eia_reg::a_data;
	default: return eia_reg::none;
	}
}

inline constexpr bool exp_id_complements_ok(uint8_t lo, uint8_t hi, uint8_t clo, uint8_t chi)
{
	return uint8_t(lo + clo) == 0 && uint8_t(hi + chi) == 0;
}

inline constexpr bool exp_id_match(uint16_t wanted, uint8_t lo, uint8_t hi, uint8_t clo, uint8_t chi)
{
	if (uint8_t(wanted) != lo)
		return false;
	if (uint8_t(wanted >> 8) != hi)
		return false;
	return exp_id_complements_ok(lo, hi, clo, chi);
}

// Which saved window matches. Returns the window index, or -1.
inline constexpr int exp_id_find(uint16_t wanted, uint8_t const lo[], uint8_t const hi[],
	uint8_t const clo[], uint8_t const chi[], int count)
{
	for (int i = 0; i < count; i++)
	{
		if (exp_id_match(wanted, lo[i], hi[i], clo[i], chi[i]))
			return i;
	}
	return -1;
}

inline constexpr bool exp_ram_512(int slot_id, uint32_t &start, uint32_t &end)
{
	if (slot_id < 0 || slot_id > 3)
		return false;
	start = EXP_RAM_BASE + uint32_t(slot_id) * EXP_RAM_BANK;
	end = start + EXP_RAM_BANK;
	return true;
}

// IDs 0 and 1 share the low megabyte of expansion RAM. IDs 2 and 3 share the high one.
inline constexpr bool exp_ram_1m(int slot_id, uint32_t &start, uint32_t &end)
{
	if (slot_id < 0 || slot_id > 3)
		return false;
	uint32_t const pair = uint32_t(slot_id & ~1);
	start = EXP_RAM_BASE + pair * EXP_RAM_BANK;
	end = start + 2u * EXP_RAM_BANK;
	return true;
}

inline constexpr bool exp_ram_1m_conflict(int slot_a, int slot_b)
{
	uint32_t a0 = 0, a1 = 0, b0 = 0, b1 = 0;
	if (!exp_ram_1m(slot_a, a0, a1) || !exp_ram_1m(slot_b, b0, b1))
		return false;
	return a0 == b0;
}

inline constexpr void exp_ram_1_5(uint32_t &start, uint32_t &end)
{
	start = EXP_RAM_BASE;
	end = EXP_RAM_BASE + 3u * EXP_RAM_BANK;
}

inline constexpr void exp_ram_2m(uint32_t &start, uint32_t &end)
{
	start = EXP_RAM_BASE;
	end = EXP_RAM_LIMIT;
}

// A write anywhere in the top eight bytes of a window resets the card.
inline constexpr bool exp_reset_write(uint32_t byte_offset)
{
	return (byte_offset & ~uint32_t(7)) == EXP_ID_HI_COMP - 7u;
}

// Motherboard RAM is the low 2 MiB. A 4 MiB machine is 2 MiB there plus
// 2 MiB of expansion. Reads inside the low 2 MiB wrap in the installed
// motherboard size. Writes past that size, still below 2 MiB, are dropped.
// Expansion RAM is decoded from 2 MiB for the installed length, with no
// identity check. The rest of the 4 MiB, up to the map RAM, reads as 0xFF.
struct ram_ref
{
	bool hit;
	uint32_t storage;
};

inline constexpr ram_ref ram_access(uint32_t addr, uint32_t board, uint32_t expansion, bool write)
{
	if (addr >= EXP_RAM_LIMIT || board == 0 || board > EXP_RAM_BASE)
		return { false, 0 };
	if (addr < EXP_RAM_BASE)
	{
		if (write)
		{
			if (addr >= board)
				return { false, 0 };
			return { true, addr };
		}
		return { true, addr % board };
	}
	if (expansion == 0 || addr >= EXP_RAM_BASE + expansion)
		return { false, 0 };
	return { true, addr };
}

// Split a single installed-byte count. 4 MiB is 2 MiB on the board and
// 2 MiB on the card. 2 MiB or less is the board alone.
struct ram_sizes
{
	uint32_t board;
	uint32_t expansion;
};

inline constexpr ram_sizes ram_installed(uint32_t bytes)
{
	if (bytes > EXP_RAM_LIMIT)
		bytes = EXP_RAM_LIMIT;
	if (bytes <= EXP_RAM_BASE)
		return { bytes, 0 };
	return { EXP_RAM_BASE, bytes - EXP_RAM_BASE };
}

} // namespace unixpc

#endif
