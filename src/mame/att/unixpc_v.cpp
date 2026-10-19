// license:GPL-2.0+
// copyright-holders:Dirk Best, R. Belmont, Visionik

#include "unixpc.h"

// 720x348 bitmap.

#define VERBOSE (0)
#include "logmacro.h"

/***************************************************************************
    VIDEO
***************************************************************************/

uint32_t unixpc_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	unixpc::rgb8 const ink = unixpc::phosphor_ink(int(m_phosphor->read()));
	m_palette->set_pen_color(0, rgb_t(0, 0, 0));
	m_palette->set_pen_color(1, rgb_t(ink.r, ink.g, ink.b));

	int const y0 = cliprect.min_y;
	int const y1 = cliprect.max_y;
	int const x0 = cliprect.min_x;
	int const x1 = cliprect.max_x;
	for (int y = y0; y <= y1; y++)
	{
		int const row = y * unixpc::SCREEN_ROW_WORDS;
		for (int x = x0; x <= x1; x++)
			bitmap.pix(y, x) = BIT(m_videoram[row + (x >> 4)], x & 15);
	}
	return 0;
}

