// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_RTC_H
#define MAME_ATT_UNIXPC_RTC_H

#pragma once

#include <cstdint>
#include <ctime>

// Toshiba TC8250-compatible nibble RTC as used on the UNIX PC.
// Registers 0-C are BCD time digits. UNIX 3.51 treats years 00-86 as
// 2000-2086, so we report year 87 (1987) to keep the kernel in the 20th
// century. This is a software quirk, not a hardware one.

namespace unixpc {

class rtc
{
public:
	static constexpr uint16_t CE  = 0x8000;
	static constexpr uint16_t ALE = 0x4000;
	static constexpr uint16_t WE  = 0x2000;

	void reset() { m_addr = 0; }

	void set_addr(uint8_t addr) { m_addr = uint8_t(addr & 0x0f); }
	uint8_t addr() const { return m_addr; }

	// Packed control word at $480000: D15=CE, D14=ALE, D13=WE, D11-D8=nibble.
	// Address latches only while ALE is set. A later CE-only 0x8000 is the
	// read strobe and must not retarget the register.
	void write_control(uint16_t data)
	{
		uint8_t const nibble = uint8_t((data >> 8) & 0x0f);
		if (data & ALE)
			m_addr = nibble;
		else if ((data & (CE | WE)) == (CE | WE))
			m_written[m_addr] = nibble;
	}

	uint8_t read_nibble() const
	{
		std::time_t const t = std::time(nullptr);
		std::tm g{};
#if defined(_WIN32)
		localtime_s(&g, &t);
#else
		localtime_r(&t, &g);
#endif
		int const year = 87; // see comment above
		switch (m_addr)
		{
		case 0x0: return uint8_t(g.tm_sec % 10);
		case 0x1: return uint8_t(g.tm_sec / 10);
		case 0x2: return uint8_t(g.tm_min % 10);
		case 0x3: return uint8_t(g.tm_min / 10);
		case 0x4: return uint8_t(g.tm_hour % 10);
		case 0x5: return uint8_t(g.tm_hour / 10);
		case 0x6: return uint8_t(g.tm_mday % 10);
		case 0x7: return uint8_t(g.tm_mday / 10);
		case 0x8: return uint8_t((g.tm_mon + 1) % 10);
		case 0x9: return uint8_t((g.tm_mon + 1) / 10);
		case 0xa: return uint8_t(year % 10);
		case 0xb: return uint8_t(year / 10);
		case 0xc: return uint8_t(g.tm_wday);
		default:  return 0;
		}
	}

	// Digit on D0-D3 (syslocal) and D8-D11 (same nibble as writes).
	uint16_t read_word() const
	{
		uint16_t const d = uint16_t(read_nibble() & 0x0f);
		return uint16_t(d | (d << 8));
	}

private:
	uint8_t m_addr = 0;
	uint8_t m_written[16]{};
};

} // namespace unixpc

#endif // MAME_ATT_UNIXPC_RTC_H
