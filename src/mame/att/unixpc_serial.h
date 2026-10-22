// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_SERIAL_H
#define MAME_ATT_UNIXPC_SERIAL_H

#pragma once

#include <cstdint>

// 838A baud generator for the built-in RS-232 channel.
//
// A write in the dialer page with A10 and A11 clear latches N from A1-A8.
// The generator output is 1.2288 MHz / (4*N). That pin is the 7201 RxC/TxC
// clock, so the channel's x16 mode yields bit rate / 16 of this frequency:
//   N=1  -> 307200 Hz (19200 baud)
//   N=2  -> 153600 Hz (9600)
//   N=16 ->  19200 Hz (1200)
//   N=64 ->   4800 Hz (300)
// N=0 stops the clock. The on-board modem channel is separate: MCR D12
// selects a fixed 19.2 kHz (x16 = 1200 baud, x64 = 300).

namespace unixpc {

inline constexpr uint32_t RS232_BAUD_XTAL = 1228800;
inline constexpr uint32_t MODEM_UART_CLOCK = 19200;

// Handler offset is the byte address in the dialer page. The kernel
// adds N itself, so the low 8 bits are the counter. Bits 9 and 10 are
// the tone strobes (A10 and A11 of a word index).
inline bool dialer_is_baud(unsigned offset)
{
	return (offset & 0x600) == 0;
}

inline unsigned dialer_baud_n(unsigned offset)
{
	return offset & 0xff;
}

inline uint32_t rs232_clock_hz(unsigned n)
{
	if (n == 0)
		return 0;
	return RS232_BAUD_XTAL / (4u * n);
}

} // namespace unixpc

#endif
