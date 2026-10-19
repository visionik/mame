// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_RTC_H
#define MAME_ATT_UNIXPC_RTC_H

#pragma once

#include <cstdint>

// Board-side RTC helpers for the UNIX PC. The chip is a Toshiba TC8250
// (machine/tc8250.h). The write port at $480000 packs CE/ALE/WE and the
// nibble; the read port at $E30000 returns the selected digit on both the
// low and high nibbles of the data bus.
//
// UNIX 3.51 treats years 00-86 as 2000-2086, so the driver forces year 87
// (1987) after the battery-backed RTC syncs from the host clock.
//
// This header stays free of emu/device includes so the Catch suite can use
// the packing helpers without linking the device.

namespace unixpc {

inline constexpr int RTC_CE_BIT  = 15;
inline constexpr int RTC_ALE_BIT = 14;
inline constexpr int RTC_WE_BIT  = 13;

inline constexpr uint16_t RTC_CE  = uint16_t(1) << RTC_CE_BIT;
inline constexpr uint16_t RTC_ALE = uint16_t(1) << RTC_ALE_BIT;
inline constexpr uint16_t RTC_WE  = uint16_t(1) << RTC_WE_BIT;

inline constexpr uint8_t rtc_control_nibble(uint16_t data)
{
	return uint8_t((data >> 8) & 0x0f);
}

// Digit mirrored onto D0-D3 and D8-D11, matching syslocal / FreeBee.
inline constexpr uint16_t rtc_read_word(uint8_t nibble)
{
	uint16_t const d = uint16_t(nibble & 0x0f);
	return uint16_t(d | (d << 8));
}

// Two-digit year the kernel stays happy with.
inline constexpr int RTC_UNIX351_YEAR = 1987;

// Register addresses (same numbering as tc8250_device).
inline constexpr uint8_t RTC_REG_S1      = 0x0;
inline constexpr uint8_t RTC_REG_S10     = 0x1;
inline constexpr uint8_t RTC_REG_MI1     = 0x2;
inline constexpr uint8_t RTC_REG_MI10    = 0x3;
inline constexpr uint8_t RTC_REG_H1      = 0x4;
inline constexpr uint8_t RTC_REG_H10     = 0x5;
inline constexpr uint8_t RTC_REG_D1      = 0x6;
inline constexpr uint8_t RTC_REG_D10     = 0x7;
inline constexpr uint8_t RTC_REG_MO1     = 0x8;
inline constexpr uint8_t RTC_REG_MO10    = 0x9;
inline constexpr uint8_t RTC_REG_Y1      = 0xa;
inline constexpr uint8_t RTC_REG_Y10     = 0xb;
inline constexpr uint8_t RTC_REG_W       = 0xc;
inline constexpr uint8_t RTC_REG_TOUT    = 0xd;
inline constexpr uint8_t RTC_REG_PROTECT = 0xe;
inline constexpr uint8_t RTC_REG_STATUS  = 0xf;

} // namespace unixpc

#endif // MAME_ATT_UNIXPC_RTC_H
