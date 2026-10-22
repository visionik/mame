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
	, device_nvram_interface(mconfig, *this)
{
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void tc8250_device::device_start()
{
	// Device clock is the 32768 Hz crystal; advance the calendar at 1 Hz.
	m_clock_timer = timer_alloc(FUNC(tc8250_device::clock_tick), this);
	attotime const tick = (clock() >= 32768)
		? attotime::from_hz(clock() / 32768)
		: attotime::from_hz(1);
	m_clock_timer->adjust(tick, 0, tick);

	save_item(NAME(m_core.m_reg));
	save_item(NAME(m_core.m_address));
	save_item(NAME(m_core.m_data));
	save_item(NAME(m_core.m_ale));
	save_item(NAME(m_core.m_ce));
	save_item(NAME(m_core.m_we));
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void tc8250_device::device_reset()
{
	// Battery-backed time/protect stay in NVRAM; only bus pins re-idle.
	m_core.m_address = 0;
	m_core.m_data = 0;
	m_core.m_ale = 0;
	m_core.m_ce = 0;
	m_core.m_we = 0;
}


//-------------------------------------------------
//  nvram_default / nvram_read / nvram_write
//-------------------------------------------------

void tc8250_device::nvram_default()
{
	m_core.reset();
}

bool tc8250_device::nvram_read(util::read_stream &file)
{
	auto const [err, actual] = read(file, m_core.m_reg, sizeof(m_core.m_reg));
	return !err && (actual == sizeof(m_core.m_reg));
}

bool tc8250_device::nvram_write(util::write_stream &file)
{
	auto const [err, actual] = write(file, m_core.m_reg, sizeof(m_core.m_reg));
	return !err && (actual == sizeof(m_core.m_reg));
}


//-------------------------------------------------
//  clock_tick - advance one second
//-------------------------------------------------

TIMER_CALLBACK_MEMBER(tc8250_device::clock_tick)
{
	advance_seconds();
}


//-------------------------------------------------
//  rtc_clock_updated - push dirtc into nibble RAM
//-------------------------------------------------

void tc8250_device::rtc_clock_updated(int year, int month, int day, int day_of_week, int hour, int minute, int second)
{
	LOG("TC8250 Clock Update: %02d-%02d-%02d %d %02d:%02d:%02d\n",
		year, month, day, day_of_week, hour, minute, second);

	m_core.write_counter(tc8250::REG_Y1, year);
	m_core.write_counter(tc8250::REG_MO1, month);
	m_core.write_counter(tc8250::REG_D1, day);
	// dirtc day_of_week is 1..7; the TC8250 weekday digit is 0..6.
	m_core.m_reg[tc8250::REG_W] = u8(day_of_week - 1);
	m_core.write_counter(tc8250::REG_H1, hour);
	m_core.write_counter(tc8250::REG_MI1, minute);
	m_core.write_counter(tc8250::REG_S1, second);
}


//-------------------------------------------------
//  commit_time - push nibble RAM back into dirtc
//-------------------------------------------------

void tc8250_device::commit_time()
{
	set_time(false,
		m_core.read_counter(tc8250::REG_Y1),
		m_core.read_counter(tc8250::REG_MO1),
		m_core.read_counter(tc8250::REG_D1),
		int(m_core.m_reg[tc8250::REG_W] % 7) + 1,
		m_core.read_counter(tc8250::REG_H1),
		m_core.read_counter(tc8250::REG_MI1),
		m_core.read_counter(tc8250::REG_S1));
}


//-------------------------------------------------
//  apply_write_result - dirtc side effects after core store
//-------------------------------------------------

void tc8250_device::apply_write_result(tc8250::core::write_result const &r)
{
	if (r.blocked)
	{
		LOG("TC8250 write blocked (protect=%x)\n", m_core.m_reg[tc8250::REG_PROTECT] & 0x0f);
		return;
	}
	if (r.zeroize.applied)
	{
		// Push nibble RAM (seconds already cleared in core) into dirtc, then
		// zeroize via adjust_seconds so a double bus strobe cannot advance
		// minutes twice (UNIX PC / Diag path).
		commit_time();
		adjust_seconds();
		return;
	}
	if (r.time_written)
		commit_time();
}


//-------------------------------------------------
//  data_w / data_r / pin writers
//-------------------------------------------------

void tc8250_device::data_w(u8 data)
{
	LOG("TC8250 data_w %x\n", data & 0x0f);
	apply_write_result(m_core.data_w(data));
}

u8 tc8250_device::data_r() const
{
	u8 const value = m_core.data_r();
	LOG("TC8250 data_r addr=%x data=%x\n", m_core.m_address, value);
	return value;
}

void tc8250_device::ale_w(int state)
{
	LOG("TC8250 ALE %d\n", state);
	m_core.ale_w(state);
}

void tc8250_device::we_w(int state)
{
	LOG("TC8250 WE %d\n", state);
	m_core.we_w(state);
}

void tc8250_device::ce_w(int state)
{
	LOG("TC8250 CE %d\n", state);
	m_core.ce_w(state);
}
