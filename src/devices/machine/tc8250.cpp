// license:BSD-3-Clause
// copyright-holders:Visionik
/**********************************************************************

    Toshiba TC8250 Real Time Clock/Calendar

**********************************************************************/

#include "emu.h"
#include "tc8250.h"

#define VERBOSE (0)
#include "logmacro.h"


DEFINE_DEVICE_TYPE(TC8250, tc8250_device, "tc8250", "Toshiba TC8250 RTC")


//-------------------------------------------------
//  tc8250_device - constructor
//-------------------------------------------------

tc8250_device::tc8250_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, TC8250, tag, owner, clock)
	, device_rtc_interface(mconfig, *this)
{
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void tc8250_device::device_start()
{
	m_clock_timer = timer_alloc(FUNC(tc8250_device::clock_tick), this);
	m_clock_timer->adjust(attotime::from_hz(clock() / 32768), 0, attotime::from_hz(clock() / 32768));

	save_item(NAME(m_reg));
	save_item(NAME(m_address));
	save_item(NAME(m_data));
	save_item(NAME(m_ale));
	save_item(NAME(m_ce));
	save_item(NAME(m_we));
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void tc8250_device::device_reset()
{
	m_address = 0;
	m_data = 0;
	m_ale = 0;
	m_ce = 0;
	m_we = 0;
	// Unlocked until a power-fail clears it (datasheet). Emulated cold
	// start keeps the key open so early writes stick; the diag also
	// writes 5 explicitly before calendar updates.
	m_reg[REG_PROTECT] = 0x05;
}


//-------------------------------------------------
//  clock_tick - advance one second
//-------------------------------------------------

TIMER_CALLBACK_MEMBER(tc8250_device::clock_tick)
{
	advance_seconds();
}


//-------------------------------------------------
//  read_counter / write_counter - pair of BCD digits
//-------------------------------------------------

int tc8250_device::read_counter(int ones_reg) const
{
	// Ten-month nibble also holds leap-year select in D2/D3 — only D0 is the tens digit.
	if (ones_reg == REG_MO1)
		return int(m_reg[REG_MO10] & 0x01) * 10 + int(m_reg[REG_MO1]);
	return (m_reg[ones_reg + 1] * 10) + m_reg[ones_reg];
}

void tc8250_device::write_counter(int ones_reg, int value)
{
	m_reg[ones_reg] = u8(value % 10);
	if (ones_reg == REG_MO1)
	{
		// Preserve L1/L0 in D3/D2 when dirtc pushes a month value.
		m_reg[REG_MO10] = u8((m_reg[REG_MO10] & 0x0c) | ((value / 10) & 0x01));
	}
	else
		m_reg[ones_reg + 1] = u8(value / 10);
}


//-------------------------------------------------
//  rtc_clock_updated - push dirtc into nibble RAM
//-------------------------------------------------

void tc8250_device::rtc_clock_updated(int year, int month, int day, int day_of_week, int hour, int minute, int second)
{
	LOG("TC8250 Clock Update: %02d-%02d-%02d %d %02d:%02d:%02d\n",
		year, month, day, day_of_week, hour, minute, second);

	write_counter(REG_Y1, year);
	write_counter(REG_MO1, month);
	write_counter(REG_D1, day);
	// dirtc day_of_week is 1..7; the TC8250 weekday digit is 0..6.
	m_reg[REG_W] = u8(day_of_week - 1);
	write_counter(REG_H1, hour);
	write_counter(REG_MI1, minute);
	write_counter(REG_S1, second);
}


//-------------------------------------------------
//  commit_time - push nibble RAM back into dirtc
//-------------------------------------------------

void tc8250_device::commit_time()
{
	set_time(false,
		read_counter(REG_Y1),
		read_counter(REG_MO1),
		read_counter(REG_D1),
		// dirtc wants 1..7
		int(m_reg[REG_W] % 7) + 1,
		read_counter(REG_H1),
		read_counter(REG_MI1),
		read_counter(REG_S1));
}


//-------------------------------------------------
//  data_w / data_r - D0-D3 bus
//-------------------------------------------------

void tc8250_device::data_w(u8 data)
{
	m_data = data & 0x0f;
	LOG("TC8250 data_w %x\n", m_data);

	// Control levels are sampled here so a packed bus cycle can set
	// ALE/CE/WE first without committing a stale nibble.
	if (m_ale)
		m_address = m_data;
	else
		maybe_write();
}

u8 tc8250_device::data_r() const
{
	u8 const addr = m_address & 0x0f;
	u8 value = m_reg[addr] & 0x0f;

	// Ten-month digit (datasheet §5.5):
	//   write: D0 = month tens, D2 = L0, D3 = L1 (D1 ignored on write)
	//   read:  D0 = month tens, D1 = 1 when year%4 matches L1:L0
	// Diag RMW of the leap-status read value writes bit1; that must not
	// retarget L0/L1 or "Leap year is not recognized" fires after a good read.
	if (addr == REG_MO10)
	{
		u8 const month_tens = m_reg[REG_MO10] & 0x01;
		u8 const leap_sel = (m_reg[REG_MO10] >> 2) & 0x03; // L1:L0 in D3:D2
		int const year = read_counter(REG_Y1);
		bool const leap = ((year % 4) == int(leap_sel));
		// Only D0 and D1 are defined on read; L1/L0 are not echoed.
		value = month_tens | (leap ? 0x02 : 0x00);
	}

	LOG("TC8250 data_r addr=%x data=%x\n", addr, value);
	return value;
}


//-------------------------------------------------
//  ale_w - address latch enable
//-------------------------------------------------

void tc8250_device::ale_w(int state)
{
	state = state ? 1 : 0;
	LOG("TC8250 ALE %d\n", state);
	m_ale = state;
	if (m_ale)
		m_address = m_data;
}


//-------------------------------------------------
//  we_w - write enable
//-------------------------------------------------

void tc8250_device::we_w(int state)
{
	state = state ? 1 : 0;
	LOG("TC8250 WE %d\n", state);
	m_we = state;
}


//-------------------------------------------------
//  ce_w - chip enable level (commit is on data_w)
//-------------------------------------------------

void tc8250_device::ce_w(int state)
{
	state = state ? 1 : 0;
	LOG("TC8250 CE %d\n", state);
	m_ce = state;
}


//-------------------------------------------------
//  maybe_write - CE+WE with ALE clear stores the nibble
//
//  The UNIX PC pulses CE alone as a read strobe after
//  selecting an address (often with a 0 nibble). That must
//  not store. Real writes assert WE as well.
//-------------------------------------------------

void tc8250_device::maybe_write()
{
	if (!m_ce || !m_we || m_ale)
		return;

	u8 const addr = m_address & 0x0f;

	// Protect key (addr E): only 5 unlocks time/calendar writes. Key itself
	// is always writable. Datasheet clears the key on power-fail.
	if (addr != REG_PROTECT && (m_reg[REG_PROTECT] & 0x0f) != 0x05)
	{
		LOG("TC8250 write blocked addr=%x (protect=%x)\n", addr, m_reg[REG_PROTECT] & 0x0f);
		return;
	}

	LOG("TC8250 write addr=%x data=%x\n", addr, m_data);

	// Status write (addr F) is "second zeroize": clear seconds; if they
	// were ≥30, carry into minutes (datasheet §5.4 / Fig 5.5).
	// Push nibble RAM into dirtc first, then use adjust_seconds() so the
	// cleared seconds land in dirtc before any minute carry. Clearing only
	// m_reg and calling advance_minutes() left dirtc seconds stale;
	// clock_updated() restored them, so a second bus strobe (UNIX PC
	// double-writes) saw secs≥30 again and advanced minutes twice —
	// diag "MINUTE counter is not maintained correctly" (59→01, sec stuck).
	if (addr == REG_STATUS)
	{
		commit_time();
		adjust_seconds();
		return;
	}

	if (addr == REG_MO10)
	{
		// Keep D0 (month tens) + D3/D2 (L1/L0); drop D1 so status RMW is safe.
		m_reg[REG_MO10] = u8((m_data & 0x0d));
	}
	else
		m_reg[addr] = m_data;

	if (addr <= REG_W)
		commit_time();
}


//-------------------------------------------------
//  control_w - board packing convenience
//-------------------------------------------------

void tc8250_device::control_w(u16 data)
{
	// FreeBee order: sample CE/ALE/WE, then present the nibble. ALE wins
	// over a CE write in the same word (address-select cycles).
	we_w(BIT(data, 13));
	ale_w(BIT(data, 14));
	ce_w(BIT(data, 15));
	data_w(u8((data >> 8) & 0x0f));
}
