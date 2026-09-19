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

TEST_CASE("unixpc rtc ALE latches address, CE-only does not", "[unixpc]")
{
	unixpc::rtc clk;
	clk.reset();
	// Month ones = register 8. CE|ALE|0x0800 then CE-only read strobe.
	clk.write_control(uint16_t(unixpc::rtc::CE | unixpc::rtc::ALE | 0x0800));
	REQUIRE(clk.addr() == 0x08);
	clk.write_control(unixpc::rtc::CE);
	REQUIRE(clk.addr() == 0x08);
	uint16_t const w = clk.read_word();
	REQUIRE((w & 0x000f) == ((w >> 8) & 0x000f));
	// Year tens = register 0xB → 8 (year 87).
	clk.write_control(uint16_t(unixpc::rtc::CE | unixpc::rtc::ALE | 0x0b00));
	clk.write_control(unixpc::rtc::CE);
	REQUIRE(clk.addr() == 0x0b);
	REQUIRE((clk.read_word() & 0x000f) == 0x0008);
	clk.write_control(uint16_t(unixpc::rtc::CE | unixpc::rtc::ALE | 0x0a00));
	clk.write_control(unixpc::rtc::CE);
	REQUIRE((clk.read_word() & 0x000f) == 0x0007);
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
