// license:BSD-3-Clause
// copyright-holders:Visionik
/**********************************************************************

    Toshiba TC8250 Real Time Clock/Calendar

    4-bit address/data RTC used on the AT&T UNIX PC (3B1/7300) and
    elsewhere. Pin writers match the chip; board glue that packs CE,
    ALE, WE and the nibble into one bus word belongs in the driver
    (see unixpc). control_w() is only a convenience for that packing.

                            _____   _____
                     NC   1 |*    \_/     | 16  Vdd
                     NC   2 |             | 15  OSC OUT
                   DATA   3 |             | 14  OSC IN
                     CE   4 |   TC8250    | 13  GND
                    ALE   5 |             | 12  D3
                     WE   6 |             | 11  D2
                     NC   7 |             | 10  D1
                     NC   8 |_____________|  9  D0

    Registers 0-C are BCD time digits. D = Tout control, E = protect
    key (write 5 to unlock), F = status / second-zeroize write.
    Ten-month digit (addr 9): D0 = month tens; D3/D2 = L1/L0 leap select;
    on read D1 = 1 when year%4 matches that select (D1 is not stored on
    write — diag RMW of the status bit must not retarget L0). Without Y2K
    support the year is two digits.

**********************************************************************/
#ifndef MAME_MACHINE_TC8250_H
#define MAME_MACHINE_TC8250_H

#pragma once

#include "dirtc.h"


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

class tc8250_device : public device_t, public device_rtc_interface
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

	// Convenience for boards that present CE/ALE/WE + nibble as one word
	// with CE=D15, ALE=D14, WE=D13, nibble=D11-D8 (UNIX PC / FreeBee).
	// Stores only when CE and WE are set and ALE is clear; CE alone is a
	// read strobe. Prefer the pin writers from new drivers.
	void control_w(u16 data);

	enum
	{
		REG_S1 = 0,
		REG_S10,
		REG_MI1,
		REG_MI10,
		REG_H1,
		REG_H10,
		REG_D1,
		REG_D10,
		REG_MO1,
		REG_MO10,
		REG_Y1,
		REG_Y10,
		REG_W,
		REG_TOUT,
		REG_PROTECT,
		REG_STATUS
	};

protected:
	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_rtc_interface overrides
	virtual bool rtc_feature_leap_year() const override { return true; }
	virtual void rtc_clock_updated(int year, int month, int day, int day_of_week, int hour, int minute, int second) override;

	TIMER_CALLBACK_MEMBER(clock_tick);

private:
	int read_counter(int ones_reg) const;
	void write_counter(int ones_reg, int value);
	void commit_time();
	void maybe_write();

	u8 m_reg[16]{};
	u8 m_address = 0;
	u8 m_data = 0;
	int m_ale = 0;
	int m_ce = 0;
	int m_we = 0;

	emu_timer *m_clock_timer = nullptr;
};


// device type definition
DECLARE_DEVICE_TYPE(TC8250, tc8250_device)

#endif // MAME_MACHINE_TC8250_H
