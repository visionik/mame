// license:BSD-3-Clause
// copyright-holders:Visionik

// mametests still vendors Catch v1.7. That copy emits x86 `int $3` on macOS
// and uses std::uncaught_exception(), so Apple Silicon cannot compile
// tests/main.cpp. Same TEST_CASE/REQUIRE file is built with Catch2 (also in
// the MAME tree) for a host run: clang++ -DUNIXPC_USE_CATCH2 ...
#ifdef UNIXPC_USE_CATCH2
#include "catch_amalgamated.hpp"
#else
#include "catch.hpp"
#if defined(__aarch64__)
#undef CATCH_TRAP
#define CATCH_TRAP() do { __asm__ volatile("brk #0"); } while (0)
#endif
#endif

#include "unixpc_mmu.h"
#include "unixpc_fault.h"
#include "unixpc_dma.h"
#include "unixpc_rtc.h"
#include "unixpc_hdc.h"
#include "unixpc_kbd.h"
#include "unixpc_serial.h"
#include "unixpc_slot.h"
#include "unixpc_io.h"
#include "unixpc_enet.h"
#include "unixpc_sound.h"
#include "unixpc_video.h"

#include <array>
#include <cstdint>

using unixpc::access;
using unixpc::mapper;
using unixpc::fault_kind;
using unixpc::dma_source;

namespace {

uint16_t present_we(uint16_t phys, uint16_t st)
{
	return uint16_t((phys & mapper::PHYS_MASK) | (st << mapper::ST_SHIFT));
}

} // namespace

TEST_CASE("unixpc mapper identity present page", "[unixpc]")
{
	std::array<uint16_t, mapper::PAGE_COUNT> ram{};
	mapper mmu;
	mmu.bind(ram.data());

	// Page 0x90 is above the 512K kernel window (0x080000).
	mmu.set_entry(0x90, present_we(0x90, 1 | mapper::ST_WRITE_ENABLE));

	uint32_t const addr = 0x00090004;
	auto p = mmu.check(addr, false, false, false);
	REQUIRE(p.result == access::ok);
	REQUIRE(p.physical == addr);

	p = mmu.translate(addr, false, false, false);
	REQUIRE(p.result == access::ok);
	REQUIRE((mmu.status_bits(mmu.entry(0x90)) & mapper::ST_PRESENT_MASK) == 2);

	p = mmu.translate(addr, true, false, false);
	REQUIRE(p.result == access::ok);
	REQUIRE((mmu.status_bits(mmu.entry(0x90)) & mapper::ST_PRESENT_MASK) == 3);
	REQUIRE((mmu.status_bits(mmu.entry(0x90)) & mapper::ST_WRITE_ENABLE) != 0);
}

TEST_CASE("unixpc mapper page fault and write protect", "[unixpc]")
{
	std::array<uint16_t, mapper::PAGE_COUNT> ram{};
	mapper mmu;
	mmu.bind(ram.data());

	REQUIRE(mmu.check(0x00100000, false, true, false).result == access::page_fault);

	mmu.set_entry(0x80, present_we(0x10, 1)); // present, not writable
	auto p = mmu.check(0x00080000, true, false, false);
	REQUIRE(p.result == access::not_writable);
	REQUIRE(p.physical == 0x00010000);

	p = mmu.check(0x00080000, false, false, false);
	REQUIRE(p.result == access::ok);
}

TEST_CASE("unixpc mapper kernel window and supervisor bypass", "[unixpc]")
{
	std::array<uint16_t, mapper::PAGE_COUNT> ram{};
	mapper mmu;
	mmu.bind(ram.data());
	mmu.set_entry(0, present_we(0, 1 | mapper::ST_WRITE_ENABLE));

	REQUIRE(mmu.check(0x00000100, false, false, false).result == access::kernel);
	REQUIRE(mmu.check(0x00000100, false, true, false).result == access::ok);
	REQUIRE(mmu.check(0x00000100, true, false, true).result == access::ok);
}

TEST_CASE("unixpc mapper supervisor write ignores WE", "[unixpc]")
{
	std::array<uint16_t, mapper::PAGE_COUNT> ram{};
	mapper mmu;
	mmu.bind(ram.data());
	// 0x204E: present, identity, no write-enable. Supervisor still writes.
	mmu.set_entry(0x4e, uint16_t(0x204e));
	auto p = mmu.check(0x0004e010, true, true, false);
	REQUIRE(p.result == access::ok);
	REQUIRE(p.physical == 0x0004e010);
	// User write-protect is only visible above the 512K kernel window.
	mmu.set_entry(0x90, uint16_t(0x2090));
	p = mmu.check(0x00090010, true, false, false);
	REQUIRE(p.result == access::not_writable);
}

TEST_CASE("unixpc mapper remaps physical page", "[unixpc]")
{
	std::array<uint16_t, mapper::PAGE_COUNT> ram{};
	mapper mmu;
	mmu.bind(ram.data());
	mmu.set_entry(5, present_we(0x3aa, 2 | mapper::ST_WRITE_ENABLE));

	auto p = mmu.translate(0x00005abc, false, true, false);
	REQUIRE(p.result == access::ok);
	REQUIRE(p.physical == ((0x3aau << 12) | 0xabc));
}

TEST_CASE("unixpc mapper high memory privilege", "[unixpc]")
{
	std::array<uint16_t, mapper::PAGE_COUNT> ram{};
	mapper mmu;
	mmu.bind(ram.data());

	// Supervisor / DMA may touch I/O space; user may not (GSR $410000).
	REQUIRE(mmu.check(0x00410000, false, true, false).result == access::ok);
	REQUIRE(mmu.check(0x00410000, false, false, true).result == access::ok);
	REQUIRE(mmu.check(0x00410000, false, false, false).result == access::privileged);

	// VIDPAL user VRAM window is allowed.
	mmu.set_vidpal(true);
	REQUIRE(mmu.check(0x00420000, false, false, false).result == access::ok);
	REQUIRE(mmu.check(0x00427fff, true, false, false).result == access::ok);
	REQUIRE(mmu.check(0x00428000, false, false, false).result == access::privileged);
	mmu.set_vidpal(false);
	REQUIRE(mmu.check(0x00420000, false, false, false).result == access::privileged);
}

TEST_CASE("unixpc cpu fault encoding", "[unixpc]")
{
	auto r = unixpc::encode_cpu_fault(fault_kind::page_fault, true, 0x00123456, 16, false);
	REQUIRE(r.genstat == 0x01ff);
	REQUIRE(r.bsr0 == uint16_t(0x7c00 | 0x12));
	REQUIRE(r.bsr1 == 0x3456);

	r = unixpc::encode_cpu_fault(fault_kind::page_fault, false, 0x00123456, 16, true);
	REQUIRE(r.genstat == uint16_t(0x41ff | 0x8400));

	r = unixpc::encode_cpu_fault(fault_kind::privileged, true, 0x00400001, 8, false);
	REQUIRE(r.genstat == 0x10ff);
	REQUIRE((r.bsr0 & 0x8000) != 0);
	REQUIRE((r.bsr0 & 0xff00) == 0xfe00); // odd byte

	r = unixpc::encode_cpu_fault(fault_kind::kernel_or_protect, false, 0x00000000, 16, false);
	REQUIRE(r.genstat == 0x51ff);

	// TST.B (A0=0): even byte, not a word. BSR lane 0x7d00, SSW $1701.
	r = unixpc::encode_cpu_fault(fault_kind::kernel_or_protect, false, 0x00000000, 8, false);
	REQUIRE(r.genstat == 0x51ff);
	REQUIRE((r.bsr0 & 0xff00) == 0x7d00);
	REQUIRE(unixpc::ssw_68010(1, true, true, true) == 0x1701);
	REQUIRE(unixpc::ssw_68010(1, true, true, false) == 0x1301);
	REQUIRE((unixpc::ssw_68010(1, true, true, true) | 0x8000) == 0x9701);

	// User write miss at 0x09A000 must keep 0x09 in BSR0 so pagein
	// does not treat it as kernel VA 0xA000.
	r = unixpc::encode_cpu_fault(fault_kind::page_fault, true, 0x0009a000, 16, true);
	REQUIRE((r.bsr0 & 0x00ff) == 0x09);
	REQUIRE(r.bsr1 == 0xa000);
	REQUIRE(r.genstat == uint16_t(0x01ff | 0x8400));
}

TEST_CASE("unixpc dma fault and nmi latch", "[unixpc]")
{
	auto r = unixpc::encode_dma_fault(true, 0x00200000, true);
	REQUIRE(r.genstat == uint16_t(0x21ff | 0x4000 | 0x8400));
	REQUIRE(r.bsr0 == 0x3c20);

	unixpc::board_status st;
	st.reset();
	REQUIRE_FALSE(st.latch_dma(r)); // EE off
	st.set_ee(true);
	REQUIRE(st.latch_dma(r));
	REQUIRE(st.nmi_latched());
	REQUIRE_FALSE(st.latch_dma(r)); // already latched
	st.clear_status();
	REQUIRE_FALSE(st.nmi_latched());
	REQUIRE((st.regs().genstat & 0x1100) == 0x1100);
}

TEST_CASE("unixpc cpu fault status freeze", "[unixpc]")
{
	unixpc::board_status st;
	st.reset();
	auto a = unixpc::encode_cpu_fault(unixpc::fault_kind::page_fault, false, 0x00080000, 16, true);
	auto b = unixpc::encode_cpu_fault(unixpc::fault_kind::page_fault, true, 0x002fffe4, 16, true);
	REQUIRE(st.latch_cpu(a));
	REQUIRE(st.regs().bsr1 == 0x0000);
	REQUIRE_FALSE(st.latch_cpu(b));
	REQUIRE(st.regs().bsr1 == 0x0000); // first miss kept
	st.clear_status();
	REQUIRE(st.latch_cpu(b));
	REQUIRE(st.regs().bsr1 == 0xffe4);
}

TEST_CASE("unixpc 60Hz CLRSINT latch", "[unixpc]")
{
	unixpc::board_status st;
	st.reset();
	st.vblank();
	REQUIRE_FALSE(st.timer_latched()); // held clear after reset
	st.set_clrsint_high(true);
	st.vblank();
	REQUIRE(st.timer_latched());
	st.set_clrsint_high(false);
	REQUIRE_FALSE(st.timer_latched());
}

TEST_CASE("unixpc dma pointer count budget", "[unixpc]")
{
	unixpc::dma_engine dma;
	dma.reset();
	dma.set_pointer_low(0x1fe);
	dma.set_pointer_high(0x1000);
	REQUIRE(dma.pointer() == ((0x1000u << 8) | 0x1fe));

	dma.set_source(dma_source::hard_disk);
	dma.set_enabled(true);
	dma.set_count(0);
	REQUIRE(dma.service(true, 8) == 8);
	for (int i = 0; i < 8; i++)
		dma.advance();
	REQUIRE(dma.count() == 8);
	REQUIRE(dma.pointer() == ((0x1000u << 8) | 0x1fe) + 16);

	dma.set_enabled(false);
	REQUIRE(dma.miss(true));
	REQUIRE_FALSE(dma.wants_transfer());
}

TEST_CASE("unixpc dma 512-byte sector fills 14-bit up-counter", "[unixpc]")
{
	unixpc::dma_engine dma;
	dma.reset();
	dma.set_source(dma_source::hard_disk);
	dma.set_enabled(true);
	// PROM writes 0xBEFF for a 256-word (512-byte) read.
	dma.set_count(0x3eff);
	REQUIRE(dma.wants_transfer());
	for (int i = 0; i < 256; i++)
		dma.advance();
	REQUIRE(dma.count() == 0x3fff);
	REQUIRE(dma.finished());
	REQUIRE_FALSE(dma.wants_transfer());
}

TEST_CASE("unixpc rtc board packing and year-87 digits", "[unixpc]")
{
	// Packed control word at $480000: D15=CE, D14=ALE, D13=WE, D11-D8=nibble.
	REQUIRE(unixpc::RTC_CE == 0x8000);
	REQUIRE(unixpc::RTC_ALE == 0x4000);
	REQUIRE(unixpc::RTC_WE == 0x2000);
	REQUIRE(unixpc::rtc_control_nibble(0x0800) == 0x08);
	REQUIRE(unixpc::rtc_control_nibble(uint16_t(unixpc::RTC_CE | unixpc::RTC_ALE | 0x0b00)) == 0x0b);
	// Read port mirrors the digit onto both nibbles of the data word.
	uint16_t const w = unixpc::rtc_read_word(0x08);
	REQUIRE((w & 0x000f) == 0x0008);
	REQUIRE(((w >> 8) & 0x000f) == 0x0008);
	// UNIX 3.51 year pin: 87 → ones=7 (reg 0xA), tens=8 (reg 0xB).
	REQUIRE(unixpc::RTC_UNIX351_YEAR == 1987);
	REQUIRE(unixpc::RTC_UNIX351_YEAR % 100 == 87);
	REQUIRE(unixpc::RTC_UNIX351_YEAR % 10 == 7);
	REQUIRE((unixpc::RTC_UNIX351_YEAR / 10) % 10 == 8);
	REQUIRE(unixpc::RTC_REG_Y1 == 0x0a);
	REQUIRE(unixpc::RTC_REG_Y10 == 0x0b);
	REQUIRE(unixpc::RTC_REG_TOUT == 0x0d);
	REQUIRE(unixpc::RTC_REG_PROTECT == 0x0e);
	REQUIRE(unixpc::RTC_REG_STATUS == 0x0f);
}

TEST_CASE("expansion slot identity and RAM windows", "[unixpc]")
{
	REQUIRE(unixpc::exp_window_base(0) == 0x00c00000u);
	REQUIRE(unixpc::exp_window_base(1) == 0x00c40000u);
	REQUIRE(unixpc::exp_window_base(7) == 0x00dc0000u);
	REQUIRE(unixpc::exp_window_base(7) + unixpc::EXP_IO_SIZE == 0x00e00000u);

	REQUIRE(unixpc::exp_id_address(unixpc::EXP_ID_LO));
	REQUIRE(unixpc::exp_id_address(unixpc::EXP_ID_HI));
	REQUIRE(unixpc::exp_id_address(unixpc::EXP_ID_LO_COMP));
	REQUIRE(unixpc::exp_id_address(unixpc::EXP_ID_HI_COMP));
	REQUIRE_FALSE(unixpc::exp_id_address(unixpc::EXP_ID_LO - 1));
	REQUIRE_FALSE(unixpc::exp_id_address(0));

	REQUIRE(unixpc::exp_id_complements_ok(0x10, 0x01, 0xf0, 0xff));
	REQUIRE_FALSE(unixpc::exp_id_complements_ok(0xff, 0xff, 0xff, 0xff));
	REQUIRE_FALSE(unixpc::exp_id_complements_ok(0x10, 0x01, 0xf0, 0x00));
	REQUIRE(unixpc::exp_id_match(0x0110, 0x10, 0x01, 0xf0, 0xff));
	REQUIRE_FALSE(unixpc::exp_id_match(0x0110, 0x10, 0x02, 0xf0, 0xfe));
	REQUIRE_FALSE(unixpc::exp_id_match(0x0110, 0x11, 0x01, 0xef, 0xff));
	REQUIRE_FALSE(unixpc::exp_id_match(0x0110, 0x10, 0x01, 0x00, 0xff));

	uint8_t lo[3] = { 0xff, 0x10, 0x20 };
	uint8_t hi[3] = { 0xff, 0x01, 0x00 };
	uint8_t clo[3] = { 0xff, 0xf0, 0xe0 };
	uint8_t chi[3] = { 0xff, 0xff, 0x00 };
	REQUIRE(unixpc::exp_id_find(0x0110, lo, hi, clo, chi, 3) == 1);
	REQUIRE(unixpc::exp_id_find(0x0020, lo, hi, clo, chi, 3) == 2);
	REQUIRE(unixpc::exp_id_find(0xffff, lo, hi, clo, chi, 3) == -1);
	REQUIRE(unixpc::exp_id_find(0x0110, lo, hi, clo, chi, 0) == -1);

	uint32_t start = 0;
	uint32_t end = 0;
	REQUIRE(unixpc::exp_ram_512(0, start, end));
	REQUIRE(start == 0x00200000u);
	REQUIRE(end == 0x00280000u);
	REQUIRE(unixpc::exp_ram_512(3, start, end));
	REQUIRE(start == 0x00380000u);
	REQUIRE(end == 0x00400000u);
	REQUIRE_FALSE(unixpc::exp_ram_512(4, start, end));
	REQUIRE_FALSE(unixpc::exp_ram_512(-1, start, end));

	REQUIRE(unixpc::exp_ram_1m(1, start, end));
	REQUIRE(start == 0x00200000u);
	REQUIRE(end == 0x00300000u);
	REQUIRE(unixpc::exp_ram_1m(2, start, end));
	REQUIRE(start == 0x00300000u);
	REQUIRE(end == 0x00400000u);
	REQUIRE(unixpc::exp_ram_1m_conflict(0, 1));
	REQUIRE_FALSE(unixpc::exp_ram_1m_conflict(1, 2));
	REQUIRE_FALSE(unixpc::exp_ram_1m_conflict(0, 9));

	unixpc::exp_ram_1_5(start, end);
	REQUIRE(start == 0x00200000u);
	REQUIRE(end == 0x00380000u);
	unixpc::exp_ram_2m(start, end);
	REQUIRE(start == 0x00200000u);
	REQUIRE(end == 0x00400000u);

	REQUIRE(unixpc::exp_reset_write(0x0003fff8u));
	REQUIRE(unixpc::exp_reset_write(0x0003ffffu));
	REQUIRE(unixpc::exp_reset_write(0x0003fffb));
	REQUIRE_FALSE(unixpc::exp_reset_write(0x0003fff0u));
	REQUIRE_FALSE(unixpc::exp_reset_write(0));

	uint32_t volatile full_bytes = 0x00400000u;
	unixpc::ram_sizes const full = unixpc::ram_installed(full_bytes);
	REQUIRE(full.board == 0x00200000u);
	REQUIRE(full.expansion == 0x00200000u);
	uint32_t volatile board_bytes = 0x00100000u;
	unixpc::ram_sizes const board_only = unixpc::ram_installed(board_bytes);
	REQUIRE(board_only.board == 0x00100000u);
	REQUIRE(board_only.expansion == 0u);
	uint32_t volatile huge_bytes = 0x00800000u;
	unixpc::ram_sizes const clamped = unixpc::ram_installed(huge_bytes);
	REQUIRE(clamped.board == 0x00200000u);
	REQUIRE(clamped.expansion == 0x00200000u);

	// 2 MiB on the board plus 2 MiB on the card is the same as a flat 4 MiB.
	for (uint32_t addr = 0; addr < 0x00400000u; addr += 0x10000u)
	{
		unixpc::ram_ref const rd = unixpc::ram_access(addr, full.board, full.expansion, false);
		unixpc::ram_ref const wr = unixpc::ram_access(addr, full.board, full.expansion, true);
		REQUIRE(rd.hit);
		REQUIRE(wr.hit);
		REQUIRE(rd.storage == addr);
		REQUIRE(wr.storage == addr);
	}
	REQUIRE_FALSE(unixpc::ram_access(0x00400000u, full.board, full.expansion, false).hit);

	// A 1 MiB board wraps reads inside that megabyte and drops writes past it.
	unixpc::ram_ref const wrapped = unixpc::ram_access(0x00180000u, 0x00100000u, 0, false);
	REQUIRE(wrapped.hit);
	REQUIRE(wrapped.storage == 0x00080000u);
	REQUIRE_FALSE(unixpc::ram_access(0x00180000u, 0x00100000u, 0, true).hit);
	REQUIRE_FALSE(unixpc::ram_access(0x00200000u, 0x00100000u, 0, false).hit);
	REQUIRE_FALSE(unixpc::ram_access(0, 0, 0, false).hit);
	uint32_t volatile oversize_board = 0x00300000u;
	REQUIRE_FALSE(unixpc::ram_access(0, oversize_board, 0, false).hit);

	unixpc::ram_ref const bank = unixpc::ram_access(0x00201000u, 0x00200000u, 0x00080000u, true);
	REQUIRE(bank.hit);
	REQUIRE(bank.storage == 0x00201000u);
	REQUIRE_FALSE(unixpc::ram_access(0x00280000u, 0x00200000u, 0x00080000u, false).hit);
	unixpc::ram_ref const combo = unixpc::ram_access(0x0037fffeu, 0x00200000u, 0x00180000u, false);
	REQUIRE(combo.hit);
	REQUIRE_FALSE(unixpc::ram_access(0x00380000u, 0x00200000u, 0x00180000u, false).hit);
}

TEST_CASE("ethernet window registers", "[unixpc]")
{
	REQUIRE(unixpc::enet_mac_octet(unixpc::ENET_MAC0) == 0);
	REQUIRE(unixpc::enet_mac_octet(unixpc::ENET_MAC_LSB) == 5);
	REQUIRE(unixpc::enet_mac_octet(unixpc::ENET_MAC0 - 1) == -1);
	REQUIRE(unixpc::enet_mac_octet(unixpc::ENET_MAC_LSB - 2) == -1);
	REQUIRE(unixpc::enet_mac_octet(0) == -1);
	REQUIRE(unixpc::enet_ring_address(0x1234) == (0x1234u << unixpc::ENET_RING_SHIFT));
	REQUIRE(unixpc::ENET_CTL_BOOT == (unixpc::ENET_DMAEN | unixpc::ENET_INT1));
	REQUIRE(unixpc::exp_id_byte(unixpc::EIA_ID, unixpc::EXP_ID_LO) == 0x10);
	REQUIRE(unixpc::exp_id_byte(unixpc::EIA_ID, unixpc::EXP_ID_HI) == 0x00);
	REQUIRE(unixpc::exp_id_byte(unixpc::EIA_ID, unixpc::EXP_ID_LO_COMP) == 0xf0);
	REQUIRE(unixpc::exp_id_byte(unixpc::EIA_ID, unixpc::EXP_ID_HI_COMP) == 0x00);
	REQUIRE(unixpc::exp_id_byte(unixpc::ENET_ID, unixpc::EXP_ID_LO) == 0x03);
	REQUIRE(unixpc::exp_id_byte(unixpc::ENET_ID, unixpc::EXP_ID_HI) == 0x10);
	REQUIRE(unixpc::exp_id_byte(unixpc::ENET_ID, unixpc::EXP_ID_LO_COMP) == 0xfd);
	REQUIRE(unixpc::exp_id_byte(unixpc::ENET_ID, unixpc::EXP_ID_HI_COMP) == 0xf0);
	REQUIRE(unixpc::exp_id_byte(unixpc::ENET_ID, 0) == -1);
	REQUIRE(unixpc::eia_decode(unixpc::EIA_B_CTRL) == unixpc::eia_reg::b_ctrl);
	REQUIRE(unixpc::eia_decode(unixpc::EIA_A_CTRL) == unixpc::eia_reg::a_ctrl);
	REQUIRE(unixpc::eia_decode(unixpc::EIA_B_DATA) == unixpc::eia_reg::b_data);
	REQUIRE(unixpc::eia_decode(unixpc::EIA_A_DATA) == unixpc::eia_reg::a_data);
	REQUIRE(unixpc::eia_decode(1) == unixpc::eia_reg::none);
	REQUIRE(unixpc::EIA_IPL == 5);
	REQUIRE(unixpc::cpu_autovector(unixpc::EIA_IPL) == 29);
	REQUIRE(unixpc::cpu_autovector_offset(unixpc::EIA_IPL) == 0x74);
	REQUIRE(unixpc::cpu_autovector(1) == 25);
	REQUIRE(unixpc::cpu_autovector_offset(1) == 0x64);
	REQUIRE(unixpc::exp_irq_bit(0) == 0);
	REQUIRE(unixpc::exp_irq_bit(1) == 1);
	REQUIRE(unixpc::exp_irq_bit(2) == 2);
	REQUIRE(unixpc::exp_irq_bit(3) == -1);
	REQUIRE(unixpc::exp_irq_bit(-1) == -1);
	REQUIRE(unixpc::lpsr_printer_bits(true, true, true, true) == unixpc::LPSR_PRINTER_FLOAT);
	REQUIRE(unixpc::lpsr_printer_bits(false, true, false, true) == ((1u << unixpc::LPSR_SELECT) | (1u << unixpc::LPSR_FAULT)));
	REQUIRE(unixpc::lp_ack_leading(true, false));
	REQUIRE_FALSE(unixpc::lp_ack_leading(false, false));
	REQUIRE_FALSE(unixpc::lp_ack_leading(true, true));
	REQUIRE(unixpc::lp_ack_trailing(false, true));
	REQUIRE_FALSE(unixpc::lp_ack_trailing(true, true));
	REQUIRE_FALSE(unixpc::lp_ack_trailing(true, false));
	REQUIRE(unixpc::lp_visible_busy(false, true));
	REQUIRE(unixpc::lp_visible_busy(true, false));
	REQUIRE_FALSE(unixpc::lp_visible_busy(false, false));
	REQUIRE(unixpc::LP_ACK_DELAY_USEC > 20);
	REQUIRE(unixpc::LPSR_PRINTER_FLOAT == 0x00f0);
	REQUIRE((unixpc::LPSR_PRINTER_FLOAT & (1u << unixpc::LPSR_BUSY)) != 0);
	REQUIRE((unixpc::LPSR_PRINTER_FLOAT & (1u << unixpc::LPSR_HDC_IRQ)) == 0);

	unixpc::enet_board board;
	board.raise_irq();
	REQUIRE(board.irq());
	board.write_control(0x00ff);
	REQUIRE(board.control() == unixpc::ENET_CTL_MASK);
	REQUIRE(board.dma_enabled());
	REQUIRE_FALSE(board.ethernet_selected());
	REQUIRE(board.interrupt_level_1());
	REQUIRE(board.read_control() == unixpc::ENET_CTL_MASK);
	REQUIRE_FALSE(board.irq());

	board.reset();
	REQUIRE(board.control() == 0);
	REQUIRE(board.ethernet_selected());
	REQUIRE_FALSE(board.dma_enabled());
	REQUIRE_FALSE(board.interrupt_level_1());
	board.capture(0x1234);
	REQUIRE(board.latch() == 0x1234);
	board.raise_irq();
	board.write_ring(0xabcd);
	REQUIRE(board.ring() == 0xabcd);
	REQUIRE_FALSE(board.irq());
	REQUIRE(board.latch() == 0x1234);
}

TEST_CASE("disk combo in use is P5.1 plus WD2010", "[unixpc]")
{
	REQUIRE(unixpc::DISK.p51());
	REQUIRE(unixpc::DISK.chip == unixpc::hdc_chip::wd2010);
	REQUIRE(unixpc::DISK.heads() == 16u);
	REQUIRE(unixpc::DISK.cylinders() == 1400u);
	REQUIRE(unixpc::DISK.cyl_hi_mask() == 0x07);
	REQUIRE(unixpc::WINCH_CYLS == 1400u);
	REQUIRE(unixpc::WINCH_HEADS == 16u);

	unixpc::disk_combo const p3_1010{ unixpc::board_rev::p3_p5, unixpc::hdc_chip::wd1010 };
	unixpc::disk_combo const p3_2010{ unixpc::board_rev::p3_p5, unixpc::hdc_chip::wd2010 };
	unixpc::disk_combo const p51_1010{ unixpc::board_rev::p51, unixpc::hdc_chip::wd1010 };
	REQUIRE(p3_1010.heads() == 8u);
	REQUIRE(p3_1010.cylinders() == 1024u);
	REQUIRE(p3_1010.cyl_hi_mask() == 0x03);
	REQUIRE(p3_2010.heads() == 8u);
	REQUIRE(p3_2010.cylinders() == 1400u);
	REQUIRE(p3_2010.cyl_hi_mask() == 0x07);
	REQUIRE(p51_1010.heads() == 16u);
	REQUIRE(p51_1010.cylinders() == 1024u);
	REQUIRE(p51_1010.cyl_hi_mask() == 0x03);
	REQUIRE(unixpc::winchester_lba(p3_1010, 0, 8, 0) == ~uint32_t(0));
	REQUIRE(unixpc::winchester_lba(p3_1010, 1024, 0, 0) == ~uint32_t(0));
	REQUIRE(unixpc::winchester_lba(p3_2010, 1024, 0, 0) == 1024u * 8u * 17u);
	REQUIRE(unixpc::winchester_lba(p51_1010, 1023, 15, 16) != ~uint32_t(0));
	REQUIRE(unixpc::winchester_lba(p51_1010, 1024, 0, 0) == ~uint32_t(0));
	REQUIRE(unixpc::winchester_sectors(p3_1010) == 1024u * 8u * 17u);
	REQUIRE(unixpc::winchester_lba_next(p3_1010, unixpc::winchester_sectors(p3_1010) - 1) == ~uint32_t(0));
	REQUIRE(unixpc::winchester_lba_next(p3_1010, 0) == 1u);

	REQUIRE(unixpc::winch_use_second(true, true));
	REQUIRE_FALSE(unixpc::winch_use_second(false, true));
	REQUIRE_FALSE(unixpc::winch_use_second(true, false));
	REQUIRE(unixpc::winch_missing_second(true, false, 0x20));
	REQUIRE_FALSE(unixpc::winch_missing_second(true, false, 0x10));
	REQUIRE_FALSE(unixpc::winch_missing_second(true, true, 0x20));
	REQUIRE_FALSE(unixpc::winch_missing_second(false, false, 0x20));
	REQUIRE(unixpc::FDC_NOT_READY == 0x80);
	REQUIRE(unixpc::SDH_DRIVE1 == 0x08);
}

TEST_CASE("unixpc winchester LBA is 1400/16/17 0-based", "[unixpc]")
{
	REQUIRE(unixpc::WINCH_SECTORS == 380800u);
	REQUIRE(unixpc::winchester_lba(0, 0, 0) == 0u);
	REQUIRE(unixpc::winchester_lba(0, 0, 16) == 16u);
	REQUIRE(unixpc::winchester_lba(0, 1, 0) == 17u);
	REQUIRE(unixpc::winchester_lba(1, 0, 0) == 272u);
	REQUIRE(unixpc::winchester_lba(1399, 15, 16) == 380799u);
	REQUIRE(unixpc::winchester_lba(1400, 0, 0) == ~uint32_t(0));
	REQUIRE(unixpc::winchester_lba(0, 16, 0) == ~uint32_t(0));
	REQUIRE(unixpc::winchester_lba(0, 0, 17) == 17u); // next-track index, still in range
	REQUIRE(unixpc::winchester_lba_next(unixpc::winchester_lba(0, 0, 16))
		== unixpc::winchester_lba(0, 1, 0));
	REQUIRE(unixpc::winchester_lba(1399, 15, 17) == ~uint32_t(0));
	// Extra head pin is 8; a live-at-BRDY decode that drops it moves
	// LBA by one head-bank (8 * 17).
	REQUIRE(unixpc::winchester_lba(32, 13, 10) - unixpc::winchester_lba(32, 5, 10)
		== 8u * unixpc::WINCH_SPT);
}

TEST_CASE("unixpc hdc dma slices are 20 words at 1 MHz", "[unixpc]")
{
	REQUIRE(unixpc::DMA_SLICE_WORDS == 20);
	REQUIRE(unixpc::SECTOR_WORDS == 256);
	REQUIRE(unixpc::hdc_dma_slices(0) == 0);
	REQUIRE(unixpc::hdc_dma_slices(20) == 1);
	REQUIRE(unixpc::hdc_dma_slices(21) == 2);
	REQUIRE(unixpc::hdc_dma_slices(256) == 13);
	REQUIRE(unixpc::hdc_dma_slices(512) == 26);
}

TEST_CASE("unixpc kbd snapshot is change-triggered makes with EOL", "[unixpc]")
{
	uint32_t now[4] = { 0, 0, 0, 0 };
	uint8_t buf[8];
	REQUIRE(unixpc::kbd_queue::fill_snapshot(now, buf, 8) == 0);
	REQUIRE_FALSE(unixpc::kbd_queue::any_down(now));

	now[3] = uint32_t(1) << 18; // R = bank3 bit 18 = 0x72
	int n = unixpc::kbd_queue::fill_snapshot(now, buf, 8);
	REQUIRE(n == 1);
	REQUIRE(buf[0] == uint8_t(0x72 | 0x80));

	now[2] = uint32_t(1) << 8; // Shift 0x48
	n = unixpc::kbd_queue::fill_snapshot(now, buf, 8);
	REQUIRE(n == 2);
	REQUIRE(buf[0] == 0x48);
	REQUIRE(buf[1] == uint8_t(0x72 | 0x80));
}

TEST_CASE("unixpc mouse packet CE flags dx dy", "[unixpc]")
{
	uint8_t p[5];
	int n = unixpc::kbd_queue::fill_mouse(3, -2, 0x04, p);
	REQUIRE(n == 4);
	REQUIRE(p[0] == 0xce);
	REQUIRE((p[1] & 0x10) != 0);
	REQUIRE((p[1] & 0x08) != 0);
	REQUIRE((p[1] & 0x04) != 0);
	REQUIRE(p[2] == 3);
	REQUIRE(p[3] == uint8_t(2 | 0x80));
	n = unixpc::kbd_queue::fill_mouse(0, 0x5f, 0, p);
	REQUIRE(n == 5);
	REQUIRE(p[3] == 0xdf);
	REQUIRE(p[4] == 0xdf);
	REQUIRE(unixpc::kbd_queue::clamp_delta(200) == 127);
	REQUIRE(unixpc::kbd_queue::clamp_delta(-200) == -127);
	REQUIRE(unixpc::kbd_queue::clamp_delta(3) == 3);
	REQUIRE(unixpc::kbd_queue::scale_delta(10, 100) == 10);
	REQUIRE(unixpc::kbd_queue::scale_delta(10, 50) == 5);
	REQUIRE(unixpc::kbd_queue::scale_delta(10, 300) == 30);
	REQUIRE(unixpc::kbd_queue::scale_delta(100, 300) == 127);
}

TEST_CASE("unixpc phosphor and 838A DTMF", "[unixpc]")
{
	REQUIRE(unixpc::SCREEN_W == 720);
	REQUIRE(unixpc::SCREEN_H == 348);
	REQUIRE(unixpc::phosphor_ink(0).r == 0xff);
	REQUIRE(unixpc::phosphor_ink(1).g == 0xff);
	REQUIRE(unixpc::phosphor_ink(2).r == 0xff);
	REQUIRE(unixpc::dtmf_row_hz(0x90) == 697);
	REQUIRE(unixpc::dtmf_col_hz(0x90) == 1209);
	REQUIRE(unixpc::dialer_speaker_on(0x0a90));
	REQUIRE_FALSE(unixpc::dialer_speaker_on(0));
}

TEST_CASE("unixpc rs232 baud generator is 1.2288 MHz over 4N", "[unixpc]")
{
	REQUIRE(unixpc::dialer_is_baud(0));
	REQUIRE(unixpc::dialer_is_baud(2));
	REQUIRE_FALSE(unixpc::dialer_is_baud(1u << 9));
	REQUIRE_FALSE(unixpc::dialer_is_baud(1u << 10));
	REQUIRE(unixpc::dialer_baud_n(2) == 2);
	REQUIRE(unixpc::dialer_baud_n(16) == 16);
	REQUIRE(unixpc::rs232_clock_hz(0) == 0);
	REQUIRE(unixpc::rs232_clock_hz(1) == 307200);
	REQUIRE(unixpc::rs232_clock_hz(2) == 153600);
	REQUIRE(unixpc::rs232_clock_hz(16) == 19200);
	REQUIRE(unixpc::rs232_clock_hz(64) == 4800);
	REQUIRE(unixpc::MODEM_UART_CLOCK == 19200);
}
