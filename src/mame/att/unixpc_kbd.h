// license:BSD-3-Clause
// copyright-holders:Visionik

#ifndef MAME_ATT_UNIXPC_KBD_H
#define MAME_ATT_UNIXPC_KBD_H

#pragma once

#include <cstdint>
#include <array>

// UNIX PC keyboard MCU protocol (bytes the 6850 delivers to the kernel).
//
// A scan report is a 60 Hz snapshot: held make scancodes with 0x80 OR'd on
// the last byte (end-of-list). All keys up is a single 0x40 on the empty
// transition. Mouse packets start with 0xce; the next keyboard report is
// then prefixed with 0xdf. Scancodes come from the TRM keycap chart.

namespace unixpc {

class kbd_queue
{
public:
	static constexpr uint8_t ALL_KEYS_UP = 0x40;
	static constexpr uint8_t AFTER_MOUSE = 0xdf;
	static constexpr uint8_t BEGIN_MOUSE = 0xce;
	static constexpr std::size_t SIZE = 256;

	void reset()
	{
		m_head = m_tail = m_len = 0;
		m_last_was_mouse = false;
	}

	bool empty() const { return m_len == 0; }
	std::size_t size() const { return m_len; }

	void push(uint8_t b)
	{
		if (m_len >= SIZE)
			return;
		m_buf[m_tail] = b;
		m_tail = (m_tail + 1) % SIZE;
		m_len++;
	}

	uint8_t pop()
	{
		if (m_len == 0)
			return 0;
		uint8_t const b = m_buf[m_head];
		m_head = (m_head + 1) % SIZE;
		m_len--;
		return b;
	}

	void begin_keyboard_if_needed()
	{
		if (m_last_was_mouse)
		{
			push(AFTER_MOUSE);
			m_last_was_mouse = false;
		}
	}

	void mark_mouse() { m_last_was_mouse = true; }

	static constexpr uint8_t LIST_END = 0x80;

	// Currently-down makes, scancode order, LIST_END on the last byte.
	static int fill_snapshot(uint32_t const now[4], uint8_t *out, int max)
	{
		int n = 0;
		uint8_t last = 0;
		bool have = false;
		for (int bank = 0; bank < 4; bank++)
		{
			for (int bit = 0; bit < 32; bit++)
			{
				if ((now[bank] & (uint32_t(1) << bit)) == 0)
					continue;
				if (have && n < max)
					out[n++] = last;
				last = uint8_t(bank * 32 + bit);
				have = true;
			}
		}
		if (have && n < max)
			out[n++] = uint8_t(last | LIST_END);
		return n;
	}

	static bool any_down(uint32_t const now[4])
	{
		return (now[0] | now[1] | now[2] | now[3]) != 0;
	}

	// 0xCE, flags (dx>0:0x10, dy<0:0x08, buttons), |dx|, |dy||0x80.
	// If Y==0xDF, duplicate it so the guest does not take it as kbd sync.
	static int clamp_delta(int d)
	{
		if (d > 127)
			return 127;
		if (d < -127)
			return -127;
		return d;
	}

	// accel_centi is 50..300 for 0.5x..3.0x
	static int scale_delta(int d, int accel_centi)
	{
		if (accel_centi < 50)
			accel_centi = 50;
		if (accel_centi > 300)
			accel_centi = 300;
		return clamp_delta((d * accel_centi) / 100);
	}

	static int fill_mouse(int dx, int dy, uint8_t buttons, uint8_t *out)
	{
		uint8_t flags = uint8_t(buttons & 0x07);
		if (dx > 0)
			flags |= 0x10;
		if (dy < 0)
			flags |= 0x08;
		int adx = dx < 0 ? -dx : dx;
		int ady = dy < 0 ? -dy : dy;
		if (adx > 127)
			adx = 127;
		if (ady > 127)
			ady = 127;
		uint8_t const yb = uint8_t(ady | 0x80);
		out[0] = BEGIN_MOUSE;
		out[1] = flags;
		out[2] = uint8_t(adx);
		out[3] = yb;
		int n = 4;
		if (yb == AFTER_MOUSE)
			out[n++] = yb;
		return n;
	}

private:
	std::array<uint8_t, SIZE> m_buf{};
	std::size_t m_head = 0, m_tail = 0, m_len = 0;
	bool m_last_was_mouse = false;
};

} // namespace unixpc

#endif // MAME_ATT_UNIXPC_KBD_H
