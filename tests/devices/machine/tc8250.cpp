// license:BSD-3-Clause
// copyright-holders:Visionik

// Protocol unit tests for the Toshiba TC8250 pure core (no full machine).
// Prefer Catch2 amalgamation (in-tree under 3rdparty/bx); fall back to Catch1.
#if __has_include("catch_amalgamated.hpp")
#include "catch_amalgamated.hpp"
#elif __has_include("../../3rdparty/bx/3rdparty/catch/catch_amalgamated.hpp")
#include "../../3rdparty/bx/3rdparty/catch/catch_amalgamated.hpp"
#else
#include "catch.hpp"
#if defined(__aarch64__)
#undef CATCH_TRAP
#define CATCH_TRAP() do { __asm__ volatile("brk #0"); } while (0)
#endif
#endif

#include "tc8250_core.h"

using tc8250::core;
using tc8250::reg_t;

namespace {

// Select register addr, then present nibble with CE+WE (store).
void write_reg(core &c, uint8_t addr, uint8_t nibble)
{
	c.ale_w(1);
	c.data_w(addr & 0x0f);
	c.ale_w(0);
	c.ce_w(1);
	c.we_w(1);
	c.data_w(nibble & 0x0f);
	c.we_w(0);
	c.ce_w(0);
}

uint8_t read_reg(core &c, uint8_t addr)
{
	c.ale_w(1);
	c.data_w(addr & 0x0f);
	c.ale_w(0);
	c.ce_w(1);
	c.we_w(0);
	uint8_t const v = c.data_r();
	c.ce_w(0);
	return v;
}

} // namespace

TEST_CASE("tc8250 ALE latches address from data bus", "[tc8250]")
{
	core c;
	c.reset();
	c.ale_w(1);
	c.data_w(0x0a); // year ones
	REQUIRE(c.m_address == 0x0a);
	c.ale_w(0);
	REQUIRE(c.m_address == 0x0a);
}

TEST_CASE("tc8250 CE alone does not store; CE+WE stores", "[tc8250]")
{
	core c;
	c.reset();
	// Unlock is default 5.
	c.ale_w(1);
	c.data_w(tc8250::REG_S1);
	c.ale_w(0);

	// Read strobe: CE, no WE, data 0 — must not wipe seconds ones.
	c.m_reg[tc8250::REG_S1] = 0x07;
	c.ce_w(1);
	c.we_w(0);
	c.data_w(0x00);
	REQUIRE(c.m_reg[tc8250::REG_S1] == 0x07);

	// Real write
	c.we_w(1);
	c.data_w(0x03);
	REQUIRE(c.m_reg[tc8250::REG_S1] == 0x03);
}

TEST_CASE("tc8250 protect key blocks calendar unless 5", "[tc8250]")
{
	core c;
	c.reset();
	write_reg(c, tc8250::REG_PROTECT, 0x00); // lock
	write_reg(c, tc8250::REG_S1, 0x09);
	REQUIRE(c.m_reg[tc8250::REG_S1] == 0x00); // blocked, stayed 0 from reset

	write_reg(c, tc8250::REG_PROTECT, 0x05); // unlock
	write_reg(c, tc8250::REG_S1, 0x09);
	REQUIRE(c.m_reg[tc8250::REG_S1] == 0x09);

	// Protect key itself always writable
	write_reg(c, tc8250::REG_PROTECT, 0x00);
	REQUIRE(c.m_reg[tc8250::REG_PROTECT] == 0x00);
}

TEST_CASE("tc8250 status write reports second zeroize carry threshold", "[tc8250]")
{
	REQUIRE(core::compute_second_zeroize(0).minute_carried == false);
	REQUIRE(core::compute_second_zeroize(29).minute_carried == false);
	REQUIRE(core::compute_second_zeroize(30).minute_carried == true);
	REQUIRE(core::compute_second_zeroize(59).minute_carried == true);

	core c;
	c.reset();
	c.write_counter(tc8250::REG_S1, 45);
	c.write_counter(tc8250::REG_MI1, 10);

	c.ale_w(1);
	c.data_w(tc8250::REG_STATUS);
	c.ale_w(0);
	c.ce_w(1);
	c.we_w(1);
	auto const r = c.data_w(0x00);
	REQUIRE(r.zeroize.applied);
	REQUIRE(r.zeroize.seconds_before == 45);
	REQUIRE(r.zeroize.minute_carried == true);
	// Seconds nibbles still hold prior value until device adjust_seconds
	REQUIRE(c.read_counter(tc8250::REG_S1) == 45);
}

TEST_CASE("tc8250 MO10 leap RMW does not retarget L0/L1", "[tc8250]")
{
	core c;
	c.reset();
	// year 87 → 87 % 4 == 3; set L1:L0 = 3 so leap recognized
	c.write_counter(tc8250::REG_Y1, 87);
	// MO10: D0=0 (month tens), D3:D2 = 3 (leap sel)
	write_reg(c, tc8250::REG_MO10, 0x0c); // binary 1100 — L1:L0=3, tens=0
	REQUIRE((c.m_reg[tc8250::REG_MO10] & 0x0d) == 0x0c);

	uint8_t const status = read_reg(c, tc8250::REG_MO10);
	REQUIRE((status & 0x01) == 0x00); // month tens
	REQUIRE((status & 0x02) != 0);    // leap recognized

	// Diag RMW writes the read value back (includes D1=1)
	write_reg(c, tc8250::REG_MO10, status);
	// L1:L0 must still be 3 (bits 3:2), D1 dropped on write
	REQUIRE((c.m_reg[tc8250::REG_MO10] & 0x0d) == 0x0c);
	REQUIRE((read_reg(c, tc8250::REG_MO10) & 0x02) != 0);
}

TEST_CASE("tc8250 counters pack BCD digit pairs", "[tc8250]")
{
	core c;
	c.reset();
	c.write_counter(tc8250::REG_H1, 23);
	REQUIRE(c.m_reg[tc8250::REG_H1] == 3);
	REQUIRE(c.m_reg[tc8250::REG_H10] == 2);
	REQUIRE(c.read_counter(tc8250::REG_H1) == 23);

	c.write_counter(tc8250::REG_MO1, 12);
	REQUIRE(c.read_counter(tc8250::REG_MO1) == 12);
	// leap bits preserved across month write
	c.m_reg[tc8250::REG_MO10] = uint8_t((c.m_reg[tc8250::REG_MO10] & 0x01) | 0x0c);
	c.write_counter(tc8250::REG_MO1, 2);
	REQUIRE((c.m_reg[tc8250::REG_MO10] & 0x0c) == 0x0c);
	REQUIRE(c.read_counter(tc8250::REG_MO1) == 2);
}
