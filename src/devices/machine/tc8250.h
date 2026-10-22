// license:BSD-3-Clause
// copyright-holders:Visionik
/**********************************************************************

    Toshiba TC8250 Real Time Clock/Calendar

    4-bit address/data RTC. Device clock is the 32768 Hz crystal (OSC);
    the internal tick is 1 Hz (clock / 32768). Board glue that packs
    CE/ALE/WE and the nibble into one bus word belongs in the driver.

    Tout (register D) is stored but not driven as an output pin yet.

    Calendar day advance follows device_rtc_interface / dirtc (Gregorian
    year%4 leap). L1:L0 in the ten-month digit only affect the
    leap-recognized bit on MO10 reads, not dirtc's advance rule.

                            _____   _____
                     NC   1 |*    \_/     | 16  Vdd
                     NC   2 |             | 15  OSC OUT
                   DATA   3 |             | 14  OSC IN
                     CE   4 |   TC8250    | 13  GND
                    ALE   5 |             | 12  D3
                     WE   6 |             | 11  D2
                     NC   7 |             | 10  D1
                     NC   8 |_____________|  9  D0

    Registers 0-C are BCD time digits. D = Tout control (stored only),
    E = protect key (write 5 to unlock), F = status / second-zeroize write.
    Ten-month digit (addr 9): D0 = month tens; D3/D2 = L1/L0 leap select;
    on read D1 = 1 when year%4 matches that select (D1 is not stored on
    write). Without Y2K support the year is two digits.

**********************************************************************/
#ifndef MAME_MACHINE_TC8250_H
#define MAME_MACHINE_TC8250_H

#pragma once

#include "dirtc.h"
#include "dinvram.h"
#include "tc8250_core.h"


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

class tc8250_device : public device_t,
	public device_rtc_interface,
	public device_nvram_interface
{
public:
	// construction/destruction
	tc8250_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);

	// Pin-level interface (chip).
	void data_w(u8 data);
	u8 data_r() const;

	void ale_w(int state);
	void ce_w(int state);
	void we_w(int state);

	using tc8250::reg_t;

protected:
	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_rtc_interface overrides
	virtual bool rtc_feature_leap_year() const override { return true; }
	virtual void rtc_clock_updated(int year, int month, int day, int day_of_week, int hour, int minute, int second) override;

	// device_nvram_interface overrides
	virtual void nvram_default() override;
	virtual bool nvram_read(util::read_stream &file) override;
	virtual bool nvram_write(util::write_stream &file) override;

	TIMER_CALLBACK_MEMBER(clock_tick);

private:
	void commit_time();
	void apply_write_result(tc8250::core::write_result const &r);

	tc8250::core m_core;
	emu_timer *m_clock_timer = nullptr;
};


// device type definition
DECLARE_DEVICE_TYPE(TC8250, tc8250_device)

#endif // MAME_MACHINE_TC8250_H
