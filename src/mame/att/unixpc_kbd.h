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

private:
	std::array<uint8_t, SIZE> m_buf{};
	std::size_t m_head = 0, m_tail = 0, m_len = 0;
	bool m_last_was_mouse = false;
};

} // namespace unixpc

#endif // MAME_ATT_UNIXPC_KBD_H
