// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_HDC_H
#define MAME_ATT_UNIXPC_HDC_H

#pragma once

#include <cstdint>
#include <cstddef>

// UNIX PC Winchester label geometry (sector 0 "UQVQ"). The raw-image
// guesser factors 380800 as 425/16/56, so WD1010 writes land at the
// wrong LBA and extend the host file. Always use the label.

namespace unixpc {

inline constexpr uint32_t WINCH_CYLS = 1400;
inline constexpr unsigned WINCH_HEADS = 16;
inline constexpr unsigned WINCH_SPT = 17;
inline constexpr uint32_t WINCH_SECTORS = WINCH_CYLS * WINCH_HEADS * WINCH_SPT; // 380800

inline uint32_t winchester_lba(uint32_t cyl, unsigned head, unsigned sec)
{
	if (cyl >= WINCH_CYLS || head >= WINCH_HEADS)
		return ~uint32_t(0);
	uint32_t const lba = (cyl * uint32_t(WINCH_HEADS) + uint32_t(head)) * uint32_t(WINCH_SPT) + uint32_t(sec);
	if (lba >= WINCH_SECTORS)
		return ~uint32_t(0);
	return lba;
}

// Next physical 512-byte sector (track wrap is LBA+1, not 5-bit wrap).
inline uint32_t winchester_lba_next(uint32_t lba)
{
	if (lba == ~uint32_t(0) || lba + 1 >= WINCH_SECTORS)
		return ~uint32_t(0);
	return lba + 1;
}

// Disk DMA is 1 MHz words interleaved with a 10 MHz CPU: ~20 words
// then ~20 µs of CPU. A 512-byte burst is 256 words ≈ 13 slices.
// BRDY is DMA count overflow, not command done; INTRQ is last word.
inline constexpr int DMA_SLICE_WORDS = 20;
inline constexpr int DMA_SLICE_USEC = 20;
inline constexpr int SECTOR_WORDS = 256;

inline int hdc_dma_slices(int words)
{
	if (words <= 0)
		return 0;
	return (words + DMA_SLICE_WORDS - 1) / DMA_SLICE_WORDS;
}

inline uint32_t crc32_ieee(uint8_t const *p, size_t n)
{
	uint32_t c = 0xffffffffu;
	for (size_t i = 0; i < n; i++)
	{
		c ^= p[i];
		for (int b = 0; b < 8; b++)
			c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
	}
	return ~c;
}

} // namespace unixpc

#endif // MAME_ATT_UNIXPC_HDC_H
