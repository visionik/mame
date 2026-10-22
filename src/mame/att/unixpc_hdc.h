// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_HDC_H
#define MAME_ATT_UNIXPC_HDC_H

#pragma once

#include <cstdint>
#include <cstddef>

// The motherboard and the Winchester chip are separate choices.
// Factory boards shipped with a socketed WD1010 (1024 cylinders, 10-bit
// cylinder register: high-byte bit 2 does not stick) and report P3...P5.
// P5.1 is the field PAL, not a second factory model: MCR2 D4 loops back
// to TSR D4, and MCR2 D0 is HDSEL3 so the drive can have 16 heads.
// A WD2010 in that same socket is the owner upgrade for an 11-bit
// cylinder (high-byte bit 2 sticks). UNIX caps it at 1400.
//
//   P3...P5 + WD1010    8 heads, 1024 cylinders
//   P3...P5 + WD2010    8 heads, 1400 cylinders
//   P5.1    + WD1010   16 heads, 1024 cylinders
//   P5.1    + WD2010   16 heads, 1400 cylinders   <-- boot default
//
// The PAL does not touch video, serial, or the phone. Its outputs are
// HDSEL3 and the D4 identity bit. A second hard disk is not part of
// that PAL. UNIX already has the nodes (/dev/fp010-/dev/fp01f) and an
// MCR2 drive-select bit (D1). Two later boards decode it:
//   ICUS (Tropiano and Kloepfer), a hand-wired TTL board
//   John Milton's board, which replaces the P5.1 PAL
// Both are one second drive on a P5.1 machine, selected by MCR2 D1
// (-hard2). Milton also decoded two more select bits for a third and
// fourth drive; UNIX never had software for those, so they are not decoded.
//
// The boot image is 1400/16/17, which matches the one combo above. The
// raw guesser factors 380800 as 425/16/56 and then extends the host file.

namespace unixpc {

enum class board_rev : uint8_t { p3_p5, p51 };
enum class hdc_chip : uint8_t { wd1010, wd2010 };

struct disk_combo
{
	board_rev board;
	hdc_chip chip;

	constexpr bool p51() const { return board == board_rev::p51; }
	constexpr unsigned heads() const { return p51() ? 16u : 8u; }
	constexpr uint32_t cylinders() const
	{
		return chip == hdc_chip::wd2010 ? 1400u : 1024u;
	}
	// WD1010: 10-bit cylinder, high byte mask 0x03. WD2010: 11-bit, 0x07.
	constexpr uint8_t cyl_hi_mask() const
	{
		return chip == hdc_chip::wd2010 ? uint8_t(0x07) : uint8_t(0x03);
	}
};

inline constexpr disk_combo DISK{ board_rev::p51, hdc_chip::wd2010 };

inline constexpr uint32_t WINCH_CYLS = DISK.cylinders();
inline constexpr unsigned WINCH_HEADS = DISK.heads();
inline constexpr unsigned WINCH_SPT = 17;
inline constexpr uint32_t WINCH_SECTORS = WINCH_CYLS * WINCH_HEADS * WINCH_SPT; // 380800

inline constexpr uint32_t winchester_sectors(disk_combo const &disk)
{
	return disk.cylinders() * uint32_t(disk.heads()) * uint32_t(WINCH_SPT);
}

inline uint32_t winchester_lba(disk_combo const &disk, uint32_t cyl, unsigned head, unsigned sec)
{
	if (cyl >= disk.cylinders() || head >= disk.heads())
		return ~uint32_t(0);
	uint32_t const lba = (cyl * uint32_t(disk.heads()) + uint32_t(head)) * uint32_t(WINCH_SPT) + uint32_t(sec);
	if (lba >= winchester_sectors(disk))
		return ~uint32_t(0);
	return lba;
}

inline uint32_t winchester_lba(uint32_t cyl, unsigned head, unsigned sec)
{
	return winchester_lba(DISK, cyl, head, sec);
}

// Next physical 512-byte sector (track wrap is LBA+1, not 5-bit wrap).
inline uint32_t winchester_lba_next(disk_combo const &disk, uint32_t lba)
{
	if (lba == ~uint32_t(0) || lba + 1 >= winchester_sectors(disk))
		return ~uint32_t(0);
	return lba + 1;
}

inline uint32_t winchester_lba_next(uint32_t lba)
{
	return winchester_lba_next(DISK, lba);
}

// Disk DMA is 1 MHz words interleaved with a 10 MHz CPU: ~20 words
// then ~20 µs of CPU. A 512-byte burst is 256 words ≈ 13 slices.
// BRDY is DMA count overflow, not command done; INTRQ is last word.
inline constexpr int DMA_SLICE_WORDS = 20;
inline constexpr int DMA_SLICE_USEC = 20;
inline constexpr int SECTOR_WORDS = 256;
inline constexpr int SECTOR_BYTES = SECTOR_WORDS * 2;

// Disk control register, low byte.
inline constexpr uint8_t DCTL_HEAD_MASK = 0x07;
inline constexpr uint8_t SDH_HEAD_MASK = 0x07;
inline constexpr uint8_t SDH_DRIVE_MASK = 0x18;
inline constexpr uint8_t SDH_DRIVE1 = 0x08;
inline constexpr unsigned WINCH_CMD_RESTORE = 1;
inline constexpr uint8_t FDC_NOT_READY = 0x80;

// MCR2 D1 selects the second image only on a P5.1 board.
inline constexpr bool winch_use_second(bool p51, bool select)
{
	return p51 && select;
}

// Restore still completes with no image. Every other command is ID-not-found.
inline constexpr bool winch_missing_second(bool second, bool present, uint8_t command)
{
	if (!second || present)
		return false;
	return (command >> 4) != WINCH_CMD_RESTORE;
}
inline constexpr uint8_t SECTOR_NUM_MASK = 0x1f;
inline constexpr uint8_t CYL_HI_MASK = DISK.cyl_hi_mask();
inline constexpr unsigned TASK_REG_MASK = 7;
inline constexpr unsigned HDSEL3_HEAD = 8;
inline constexpr int DCTL_HD_SEL = 3;
inline constexpr int DCTL_HDC_RESET = 4;
inline constexpr int DCTL_MOTOR = 5;
inline constexpr int DCTL_FD_SEL = 6;

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
