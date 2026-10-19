// license:GPL-2.0+
// copyright-holders:Dirk Best, R. Belmont, Visionik

#include "unixpc.h"

// 838A dialer tones.

#define VERBOSE (0)
#include "logmacro.h"

void unixpc_state::dialer_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	// 838A: the low 8 bits of the access offset are the payload.
	// Bit 9 loads the low tone byte; bit 10 loads the high byte and
	// starts the tone. Both clear latches the RS-232 baud counter.
	(void)data;
	(void)mem_mask;
	unsigned const word = unsigned(offset);
	{
		static int n;
		if (n < 24)
		{
			++n;
			osd_printf_info("dialer off=%u\n", word);
		}
	}
	uint8_t const byte = uint8_t(unixpc::dialer_baud_n(word));
	if (BIT(word, 9))
		m_dialer_word = uint16_t((m_dialer_word & unixpc::HI_BYTE) | byte);
	else if (BIT(word, 10))
	{
		m_dialer_word = uint16_t((m_dialer_word & unixpc::LO_BYTE) | (uint16_t(byte) << 8));
		bool const on = unixpc::dialer_speaker_on(m_dialer_word);
		uint8_t const tone = uint8_t(m_dialer_word & unixpc::DIALER_TONE_MASK);
		m_beep_lo->set_clock(uint32_t(unixpc::dtmf_row_hz(tone)));
		m_beep_hi->set_clock(uint32_t(unixpc::dtmf_col_hz(tone)));
		m_beep_lo->set_state(on ? 1 : 0);
		m_beep_hi->set_state(on ? 1 : 0);
	}
	else if (unixpc::dialer_is_baud(word))
	{
		unsigned const n = unixpc::dialer_baud_n(word);
		uint32_t const hz = unixpc::rs232_clock_hz(n);
		m_rs232_brg->set_clock(hz);
		static unsigned last_n = ~0u;
		if (n != last_n)
		{
			last_n = n;
			osd_printf_info("RS-232 baud n=%u clock=%u\n", n, hz);
		}
		LOG("rs232 baud n=%u clock=%u\n", n, hz);
	}
}

