// license:BSD-3-Clause
// copyright-holders:Visionik
/**********************************************************************

    Toshiba TC8250 — pure nibble protocol / register model

    No emu dependencies. The device wrapper (tc8250.h) owns dirtc, NVRAM,
    and the 1 Hz tick; this core owns ALE/CE/WE, protect, leap MO10, and
    second-zeroize arithmetic so protocol can be unit-tested.

**********************************************************************/
#ifndef MAME_MACHINE_TC8250_CORE_H
#define MAME_MACHINE_TC8250_CORE_H

#pragma once

#include <cstdint>
#include <cstring>

namespace tc8250 {

enum reg_t : uint8_t
{
	REG_S1 = 0,
	REG_S10,
	REG_MI1,
	REG_MI10,
	REG_H1,
	REG_H10,
	REG_D1,
	REG_D10,
	REG_MO1,
	REG_MO10,
	REG_Y1,
	REG_Y10,
	REG_W,
	REG_TOUT,
	REG_PROTECT,
	REG_STATUS
};

// Result of a status (addr F) write: second-zeroize per datasheet §5.4.
struct second_zeroize
{
	bool applied = false;
	bool minute_carried = false;
	uint8_t seconds_before = 0;
};

class core
{
public:
	// Public for device save_item / NVRAM; treat as device state.
	uint8_t m_reg[16]{};
	uint8_t m_address = 0;
	uint8_t m_data = 0;
	int m_ale = 0;
	int m_ce = 0;
	int m_we = 0;

	void reset()
	{
		std::memset(m_reg, 0, sizeof(m_reg));
		m_address = 0;
		m_data = 0;
		m_ale = 0;
		m_ce = 0;
		m_we = 0;
		// Unlocked until power-fail clears it (datasheet). Cold start keeps
		// the key open so early writes stick; software may also write 5.
		m_reg[REG_PROTECT] = 0x05;
	}

	void ale_w(int state)
	{
		m_ale = state ? 1 : 0;
		if (m_ale)
			m_address = m_data;
	}

	void ce_w(int state) { m_ce = state ? 1 : 0; }
	void we_w(int state) { m_we = state ? 1 : 0; }

	struct write_result
	{
		second_zeroize zeroize;
		bool time_written = false;
		bool blocked = false;
	};

	// Returns a write_result if a store was attempted (ALE clear); empty
	// result if only the address latched or nothing ran.
	write_result data_w(uint8_t data)
	{
		m_data = data & 0x0f;
		// Control levels are sampled here so a packed bus cycle can set
		// ALE/CE/WE first without committing a stale nibble.
		if (m_ale)
		{
			m_address = m_data;
			return {};
		}
		return maybe_write();
	}

	uint8_t data_r() const
	{
		uint8_t const addr = m_address & 0x0f;
		uint8_t value = m_reg[addr] & 0x0f;

		// Ten-month digit (datasheet §5.5):
		//   write: D0 = month tens, D2 = L0, D3 = L1 (D1 ignored on write)
		//   read:  D0 = month tens, D1 = 1 when year%4 matches L1:L0
		if (addr == REG_MO10)
		{
			uint8_t const month_tens = m_reg[REG_MO10] & 0x01;
			uint8_t const leap_sel = (m_reg[REG_MO10] >> 2) & 0x03;
			int const year = read_counter(REG_Y1);
			bool const leap = ((year % 4) == int(leap_sel));
			value = month_tens | (leap ? 0x02 : 0x00);
		}
		return value;
	}

	write_result maybe_write()
	{
		write_result r;
		if (!m_ce || !m_we || m_ale)
			return r;

		uint8_t const addr = m_address & 0x0f;

		if (addr != REG_PROTECT && (m_reg[REG_PROTECT] & 0x0f) != 0x05)
		{
			r.blocked = true;
			return r;
		}

		if (addr == REG_STATUS)
		{
			// Do not clear seconds here. Device commit_time() must push the
			// prior seconds into dirtc, then adjust_seconds() zeroizes and
			// may carry the minute (clock_updated rewrites the nibbles).
			r.zeroize = compute_second_zeroize(read_counter(REG_S1));
			return r;
		}

		if (addr == REG_MO10)
		{
			// D0 = month tens from data; D1 ignored (leap-recognized is read-only).
			// D3/D2 = L1:L0 from data when present; if the write is a Diag-style
			// RMW of a MO10 *read* value (D3:D2 clear, D1 set), keep prior L1:L0
			// so leap select is not silently cleared.
			uint8_t next = uint8_t(m_data & 0x01);
			if ((m_data & 0x0c) != 0)
				next |= uint8_t(m_data & 0x0c);
			else
				next |= uint8_t(m_reg[REG_MO10] & 0x0c);
			m_reg[REG_MO10] = next;
		}
		else
			m_reg[addr] = m_data;

		if (addr <= REG_W)
			r.time_written = true;
		return r;
	}

	static second_zeroize compute_second_zeroize(int seconds_before)
	{
		second_zeroize z;
		z.applied = true;
		z.seconds_before = uint8_t(seconds_before);
		z.minute_carried = (seconds_before >= 30);
		return z;
	}

	int read_counter(int ones_reg) const
	{
		if (ones_reg == REG_MO1)
			return int(m_reg[REG_MO10] & 0x01) * 10 + int(m_reg[REG_MO1]);
		return (m_reg[ones_reg + 1] * 10) + m_reg[ones_reg];
	}

	void write_counter(int ones_reg, int value)
	{
		m_reg[ones_reg] = uint8_t(value % 10);
		if (ones_reg == REG_MO1)
			m_reg[REG_MO10] = uint8_t((m_reg[REG_MO10] & 0x0c) | ((value / 10) & 0x01));
		else
			m_reg[ones_reg + 1] = uint8_t(value / 10);
	}
};

} // namespace tc8250

#endif // MAME_MACHINE_TC8250_CORE_H
