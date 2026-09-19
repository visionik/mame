// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_MMU_H
#define MAME_ATT_UNIXPC_MMU_H

#pragma once

#include <cstdint>
#include <cstddef>

// AT&T UNIX PC (3B1 / 7300) page mapper.
//
// 1024 map-RAM entries, one 16-bit word per 4K logical page:
//   bits 9-0   physical page
//   bits 14-13 presence/accessed/dirty (0=absent, 1=present, 2=read, 3=dirty)
//   bit  15    write enable
// Logical addresses below 0x080000 are kernel space for user-mode accesses.
// Addresses at or above 0x400000 are not translated by this mapper.

namespace unixpc {

enum class access : uint8_t
{
	ok = 0,
	page_fault,
	not_writable,
	kernel,
	privileged
};

struct probe
{
	access result = access::page_fault;
	uint32_t physical = 0;
};

class mapper
{
public:
	static constexpr uint32_t PAGE_SHIFT = 12;
	static constexpr uint32_t PAGE_SIZE = uint32_t(1) << PAGE_SHIFT;
	static constexpr uint32_t PAGE_COUNT = 1024;
	static constexpr uint32_t RAM_WINDOW = PAGE_COUNT * PAGE_SIZE; // 4 MiB
	static constexpr uint32_t KERNEL_WINDOW = 0x080000;

	static constexpr uint16_t PHYS_MASK = 0x03ff;
	static constexpr uint16_t ST_SHIFT = 13;
	static constexpr uint16_t ST_PRESENT_MASK = 0x03;
	static constexpr uint16_t ST_WRITE_ENABLE = 0x04;

	void bind(uint16_t *entries) { m_map = entries; }
	uint16_t *data() { return m_map; }
	uint16_t const *data() const { return m_map; }

	void set_vidpal(bool enabled) { m_vidpal = enabled; }
	bool vidpal() const { return m_vidpal; }

	uint16_t entry(uint16_t page) const
	{
		return m_map != nullptr ? m_map[page & (PAGE_COUNT - 1)] : 0;
	}

	void set_entry(uint16_t page, uint16_t value)
	{
		if (m_map != nullptr)
			m_map[page & (PAGE_COUNT - 1)] = value;
	}

	static uint16_t page_index(uint32_t logical)
	{
		return uint16_t((logical >> PAGE_SHIFT) & (PAGE_COUNT - 1));
	}

	static uint16_t status_bits(uint16_t map_word)
	{
		return uint16_t(map_word >> ST_SHIFT);
	}

	static uint32_t physical_of(uint16_t map_word, uint32_t logical)
	{
		uint32_t const phys_page = uint32_t(map_word & PHYS_MASK);
		return (phys_page << PAGE_SHIFT) | (logical & (PAGE_SIZE - 1));
	}

	// Permission check only. Does not update accessed/dirty bits.
	probe check(uint32_t logical, bool write, bool supervisor, bool dma) const
	{
		probe p;
		if (logical >= RAM_WINDOW)
		{
			p.result = access::privileged;
			p.physical = logical;
			return p;
		}

		uint16_t const word = entry(page_index(logical));
		uint16_t const st = status_bits(word);
		p.physical = physical_of(word, logical);

		if ((st & ST_PRESENT_MASK) == 0)
		{
			p.result = access::page_fault;
			return p;
		}

		if (dma || supervisor)
		{
			p.result = access::ok;
			return p;
		}

		if (logical < KERNEL_WINDOW)
		{
			p.result = access::kernel;
			return p;
		}

		if (write && (st & ST_WRITE_ENABLE) == 0)
		{
			p.result = access::not_writable;
			return p;
		}

		p.result = access::ok;
		return p;
	}

	// Translate and, if allowed, mark the page accessed or dirty.
	probe translate(uint32_t logical, bool write, bool supervisor, bool dma)
	{
		probe const p = check(logical, write, supervisor, dma);
		if (p.result == access::ok && logical < RAM_WINDOW)
			touch(page_index(logical), write);
		return p;
	}

private:
	void touch(uint16_t page, bool write)
	{
		if (m_map == nullptr)
			return;

		uint16_t word = m_map[page];
		uint16_t const st = status_bits(word) & ST_PRESENT_MASK;
		if (st == 1)
		{
			word &= uint16_t(~(uint16_t(0x03) << ST_SHIFT));
			word |= uint16_t((write ? 3 : 2) << ST_SHIFT);
			m_map[page] = word;
		}
		else if (st == 2 && write)
		{
			word |= uint16_t(3 << ST_SHIFT);
			m_map[page] = word;
		}
	}

	uint16_t *m_map = nullptr;
	bool m_vidpal = false;
};

} // namespace unixpc

#endif // MAME_ATT_UNIXPC_MMU_H
