// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_VIDEO_H
#define MAME_ATT_UNIXPC_VIDEO_H

#pragma once

#include <cstdint>

namespace unixpc {

inline constexpr int SCREEN_W = 720;
inline constexpr int SCREEN_H = 348;
inline constexpr int SCREEN_ROW_WORDS = SCREEN_W / 16;

struct rgb8
{
	uint8_t r, g, b;
};

// Phosphor: 0 white, 1 P1 green, 2 P3 amber.
inline rgb8 phosphor_ink(int sel)
{
	switch (sel & 3)
	{
	case 1: return rgb8{ 0x33, 0xff, 0x66 };
	case 2: return rgb8{ 0xff, 0xb4, 0x3c };
	default: return rgb8{ 0xff, 0xff, 0xff };
	}
}

} // namespace unixpc

#endif
