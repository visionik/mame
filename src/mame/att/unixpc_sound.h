// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_SOUND_H
#define MAME_ATT_UNIXPC_SOUND_H

#pragma once

#include <cstdint>

namespace unixpc {

// 838A: tone nibble is row (bits 1-0) × column (bits 3-2). Speaker is bit 9.
inline constexpr uint16_t DIALER_TONE_MASK = 0x00ff;
inline constexpr uint16_t DIALER_SPEAKER = 0x0200;
inline constexpr int DTMF_ROW_HZ[4] = { 697, 770, 852, 941 };
inline constexpr int DTMF_COL_HZ[4] = { 1209, 1477, 1336, 1336 };

inline int dtmf_row_hz(uint8_t tone)
{
	return DTMF_ROW_HZ[tone & 3];
}

inline int dtmf_col_hz(uint8_t tone)
{
	return DTMF_COL_HZ[(tone >> 2) & 3];
}

inline bool dialer_speaker_on(uint16_t ctrl)
{
	return ((ctrl & DIALER_SPEAKER) != 0) && ((ctrl & DIALER_TONE_MASK) != 0);
}

} // namespace unixpc

#endif
