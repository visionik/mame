// license:GPL-2.0+
// copyright-holders:Dirk Best, R. Belmont
/***************************************************************************

    AT&T UNIX PC series (7300 and 3B1)

    Original skeleton: Dirk Best and R. Belmont

    The 3B1 is a 10 MHz 68010 with a custom 4K-page mapper (not a 68851),
    WD2797 floppy, WD2010-class MFM hard disk, and a 720x348 mono bitmap.
    UNIX 3.51 demand-pages through map RAM at 0x400000; faults report via
    GENSTAT/BSR0/BSR1 and a 68010 bus error (CPU) or level-7 NMI (DMA).

    Disk DMA is a board engine: 24-bit pointer, 14-bit word count, DRQ
    from the selected controller. The 60 Hz interrupt is a latch cleared
    by MCR.CLRSINT- (active low), not a timer enable bit.

***************************************************************************/


#include "emu.h"

#include "bus/centronics/ctronics.h"
#include "bus/rs232/rs232.h"
#include "cpu/m68000/m68010.h"
#include "imagedev/floppy.h"
#include "imagedev/harddriv.h"
#include "machine/6850acia.h"
#include "machine/74259.h"
#include "machine/input_merger.h"
#include "machine/output_latch.h"
#include "machine/ram.h"
//#include "machine/tc8250.h"
#include "machine/wd1010.h"
#include "machine/wd_fdc.h"
#include "machine/z80sio.h"

#include "emupal.h"
#include "render.h"
#include "screen.h"
#include "speaker.h"
#include "sound/beep.h"

#include "unixpc.lh"
#include "unixpc_dma.h"
#include "unixpc_fault.h"
#include "unixpc_hdc.h"
#include "unixpc_kbd.h"
#include "unixpc_mmu.h"
#include "unixpc_rtc.h"
#include "unixpc_sound.h"
#include "unixpc_video.h"

#include <algorithm>
#include <cstdio>

#ifndef UNIXPC_EXTRA_LOG
#define UNIXPC_EXTRA_LOG 0
#endif
#ifndef UNIXPC_HDC_TRACE
#define UNIXPC_HDC_TRACE 0
#endif
#ifndef UNIXPC_USER_TRACE
#define UNIXPC_USER_TRACE 0
#endif
#ifndef UNIXPC_BERR_TRACE
#define UNIXPC_BERR_TRACE 0
#endif

#define VERBOSE (0)
#include "logmacro.h"


/***************************************************************************
    DRIVER STATE
***************************************************************************/

namespace {

class unixpc_state : public driver_device
{
public:
	unixpc_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_gcr(*this, "gcr"),
		m_tcr(*this, "tcr"),
		m_int02(*this, "int02"),
		m_ram(*this, RAM_TAG),
		m_wd2797(*this, "wd2797"),
		m_floppy(*this, "wd2797:0:525dd"),
		m_hdc(*this, "hdc"),
		m_hdr0(*this, "hdc:0"),
		m_kbc(*this, "kbc"),
		m_keys(*this, "KEY%u", 0U),
		m_mouse_x(*this, "MOUSEX"),
		m_mouse_y(*this, "MOUSEY"),
		m_mouse_b(*this, "MOUSEB"),
		m_phosphor(*this, "PHOSPHOR"),
		m_mouse_acc(*this, "MOUSEACC"),
		m_winscale(*this, "WINSCALE"),
		m_palette(*this, "palette"),
		m_beep_lo(*this, "beep_lo"),
		m_beep_hi(*this, "beep_hi"),
		m_ramromview(*this, "ramromview"),
		m_mapram(*this, "mapram"),
		m_videoram(*this, "videoram"),
		m_bootrom(*this, "bootrom")
	{ }

	void unixpc(machine_config &config);

private:
	uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	uint16_t line_printer_r();
	void disk_control_w(uint8_t data);
	void gcr_w(offs_t offset, uint16_t data);
	void romlmap_w(int state);
	void error_enable_w(int state);
	void parity_enable_w(int state);
	void bpplus_w(int state);
	void vblank_w(int state);
	uint16_t ram_mmu_r(offs_t offset);
	void ram_mmu_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void ram_phys_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	uint16_t mapram_r(offs_t offset);
	void mapram_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	uint16_t bootrom_mirror_r(offs_t offset);
	void apply_cpu_fault(unixpc::access result, uint32_t logical, bool write);
	uint16_t gsr_r();
	void tcr_w(offs_t offset, uint16_t data);
	uint16_t tsr_r();
	uint16_t rtc_r();
	void rtc_w(uint16_t data);
	uint16_t diskdma_size_r();
	void diskdma_size_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void diskdma_ptr_w(offs_t offset, uint16_t data);

	uint16_t bsr0_r();
	uint16_t bsr1_r();
	void csr_w(uint16_t data);
	uint16_t csr_r();
	void mcr2_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	void wd2797_intrq_w(int state);
	void wd2797_drq_w(int state);
	uint8_t fdc_r(offs_t offset);
	void fdc_w(offs_t offset, uint8_t data);
	uint16_t fdc16_r(offs_t offset, uint16_t mem_mask = ~0);
	void fdc16_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void wd1010_intrq_w(int state);
	void hdc_bdrq_w(int state);
	void hdc_bcr_w(int state);
	void hdc_out_data(uint8_t data);
	uint8_t hdc_in_data();
	void hdc_reg_w(offs_t offset, uint8_t data);
	uint8_t hdc_reg_r(offs_t offset);
	void hdc_word_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	uint16_t hdc_word_r(offs_t offset, uint16_t mem_mask = ~0);
	uint16_t modem_r(offs_t offset);
	void modem_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void dialer_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);

	TIMER_CALLBACK_MEMBER(dma_tick);
	TIMER_CALLBACK_MEMBER(kbd_clock);
	TIMER_CALLBACK_MEMBER(hdc_brdy_tick);

	void unixpc_mem(address_map &map) ATTR_COLD;

	bool dma_store_word(uint16_t word);
	uint16_t dma_fetch_word();
	void dma_service_floppy();
	void dma_service_hdc();
#if UNIXPC_HDC_TRACE
	void hdc_log_burst();
#endif
	void nmi_update();
	void kbd_scan();
	void kbd_shift_bit();
	void hdc_apply_head();
	void unixpc_bind_ui_menu();
#if UNIXPC_BERR_TRACE
	void berr_trace(char const *ev, uint32_t fa, uint16_t ssw);
	uint16_t peek_super16(uint32_t addr);
	void maybe_berr_kpc(uint16_t fc, uint32_t logical);
#endif
#if UNIXPC_USER_TRACE
	void user_trace(char const *ev, uint32_t va, uint32_t crc, char const *extra);
	uint32_t user_crc_logical(uint32_t addr, unsigned nbytes, bool super);
	void maybe_user_trace(uint16_t fc, uint32_t logical, unixpc::probe const &p, bool write);
#endif

private:
	required_device<m68010_device> m_maincpu;
	required_device<ls259_device> m_gcr;
	required_device<ls259_device> m_tcr;
	required_device<input_merger_device> m_int02;
	required_device<ram_device> m_ram;
	required_device<wd2797_device> m_wd2797;
	required_device<floppy_image_device> m_floppy;
	required_device<wd1010_device> m_hdc;
	required_device<harddisk_image_device> m_hdr0;
	required_device<acia6850_device> m_kbc;
	optional_ioport_array<4> m_keys;
	required_ioport m_mouse_x;
	required_ioport m_mouse_y;
	required_ioport m_mouse_b;
	required_ioport m_phosphor;
	required_ioport m_mouse_acc;
	required_ioport m_winscale;
	required_device<palette_device> m_palette;
	required_device<beep_device> m_beep_lo;
	required_device<beep_device> m_beep_hi;
	memory_view m_ramromview;

	required_shared_ptr<uint16_t> m_mapram;
	required_shared_ptr<uint16_t> m_videoram;
	required_memory_region m_bootrom;

	uint16_t *m_ramptr = nullptr;
	uint32_t m_ramsize = 0;
	bool m_fdc_intrq = false;
	bool m_hdc_intrq = false;

	unixpc::mapper m_mapper;
	unixpc::board_status m_board;
	unixpc::dma_engine m_dma;
	unixpc::rtc m_rtc;
	unixpc::kbd_queue m_kbd;

	emu_timer *m_dma_timer = nullptr;
	emu_timer *m_kbd_timer = nullptr;
	emu_timer *m_hdc_brdy_timer = nullptr;

	uint8_t m_hdc_buf[512]{};
	int m_hdc_pos = 0;
	bool m_fdc_drq = false;

	uint8_t m_hdc_head_low = 0;
	bool m_hdc_head4 = false;
	bool m_mcr2_p51 = false;
	uint16_t m_hdc_cyl = 0;
	uint8_t m_hdc_sector = 0;
	uint8_t m_hdc_seccnt = 0;
	uint32_t m_hdc_next_lba = ~uint32_t(0);
#if UNIXPC_HDC_TRACE
	int m_hdc_trace_n = 0;
	uint32_t m_hdc_log_lba = ~uint32_t(0);
	unsigned m_hdc_log_head = 0;
	unsigned m_hdc_log_sec = 0;
#endif
	int m_hdc_dma_idx = 0;
	int m_hdc_dma_words = 0;
	bool m_hdc_bdrq = false;
	uint8_t m_fdc_hi = 0;
	bool m_fdc_have_hi = false;
	bool m_fdc_irq_latch = false;

	uint32_t m_key_down[4]{};
	uint32_t m_key_seen[4]{};
	int m_key_stable = 0;
	bool m_keys_held = false;
	int m_mouse_lx = 0;
	int m_mouse_ly = 0;
	uint8_t m_mouse_lb = 0;
	uint8_t m_mouse_sent_b = 0;
	int m_mouse_pdx = 0;
	int m_mouse_pdy = 0;
	bool m_mouse_inited = false;
	bool m_uncap_held = false;
	int m_win_view = -1;
	uint16_t m_dialer_word = 0;
	uint16_t m_kbd_shift = 0;
	int m_kbd_bits = 0;
	int m_kbd_cell = 0;
	bool m_kbd_rxd = true;
};


/***************************************************************************
    MEMORY
***************************************************************************/

void unixpc_state::gcr_w(offs_t offset, uint16_t data)
{
	m_gcr->write_bit(offset >> 11, BIT(data, 15));
}

uint16_t unixpc_state::mapram_r(offs_t offset)
{
	return m_mapram[offset];
}

void unixpc_state::mapram_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
#if UNIXPC_EXTRA_LOG
	uint16_t const old = m_mapram[offset];
#endif
	COMBINE_DATA(&m_mapram[offset]);
#if UNIXPC_EXTRA_LOG
	if (old != m_mapram[offset])
		LOG("map[%03x] %04x->%04x pc=%06x\n",
			unsigned(offset), old, m_mapram[offset], unsigned(m_maincpu->pc()));
#endif
}

void unixpc_state::romlmap_w(int state)
{
	LOG("romlmap %d pc=%06x\n", state, unsigned(m_maincpu->pc()));
	m_ramromview.select(state ? 1 : 0);
}

void unixpc_state::apply_cpu_fault(unixpc::access result, uint32_t logical, bool write)
{
	if (machine().side_effects_disabled())
		return;

	int const width = (m_maincpu->get_access_sz() == 1) ? 8 : 16;
	auto const enc = unixpc::encode_cpu_fault(unixpc::kind_from(result), write, logical, width, m_board.pie());
	// Latch GSR/BSR on the first miss only (nested must not refresh).
	bool const latched = m_board.latch_cpu(enc);
	u16 const fc = m_maincpu->get_fc();
	LOG("cpu fault res=%d addr=%06x wr=%d pc=%06x fc=%u latch=%d gsr=%04x\n",
		int(result), logical, int(write), unsigned(m_maincpu->pc()),
		unsigned(fc), int(latched), m_board.regs().genstat);
	(void)latched;
	if (m_board.ee())
		m_maincpu->set_buserror_details(logical, write ? 0 : 1, fc, true);
#if UNIXPC_BERR_TRACE
	if (fc == 1 && logical < 8 && !write)
	{
		u16 const sz = m_maincpu->get_access_sz();
		uint16_t const ssw = unixpc::ssw_68010(fc, true, sz == 1, (logical & 1) == 0);
		berr_trace("BERR", logical, ssw);
	}
#endif
}

#if UNIXPC_BERR_TRACE
namespace {
int g_berr_kpc_left;
bool g_berr_want_frame;
int g_berr_seqs;
}

uint16_t unixpc_state::peek_super16(uint32_t addr)
{
	unixpc::probe const q = m_mapper.check(addr, false, true, false);
	if (q.result != unixpc::access::ok || q.physical >= m_ramsize || m_ramptr == nullptr)
		return 0xffff;
	return m_ramptr[q.physical >> 1];
}

void unixpc_state::berr_trace(char const *ev, uint32_t fa, uint16_t ssw)
{
	static FILE *tf;
	static int n;
	if (tf == nullptr)
	{
		tf = fopen("/tmp/mame_berr.tsv", "w");
		if (tf != nullptr)
			fprintf(tf, "n\tt_us\tev\tpc\tsr\ta7\tssw\tfa\tgsr\tbsr0\tbsr1\textra\n");
	}
	if (tf == nullptr)
		return;
	if (ev[0] == 'B' && ev[1] == 'E')
	{
		if (g_berr_seqs >= 3)
			return;
		g_berr_seqs++;
		g_berr_want_frame = true;
		g_berr_kpc_left = 24;
	}
	uint64_t const t_us = uint64_t(machine().time().as_ticks(1000000));
	uint32_t const pc = unsigned(m_maincpu->pc());
	uint32_t const sr = uint32_t(m_maincpu->state_int(M68K_SR));
	uint32_t const a7 = uint32_t(m_maincpu->state_int(M68K_A7));
	fprintf(tf, "%d\t%llu\t%s\t%06x\t%04x\t%08x\t%04x\t%08x\t%04x\t%04x\t%04x\t-\n",
		n++, (unsigned long long)t_us, ev, pc, unsigned(sr), a7, ssw, fa,
		m_board.regs().genstat, m_board.regs().bsr0, m_board.regs().bsr1);
	fflush(tf);
}

void unixpc_state::maybe_berr_kpc(uint16_t fc, uint32_t logical)
{
	static FILE *tf;
	if (g_berr_kpc_left <= 0 && !g_berr_want_frame)
		return;
	if (!(fc == 2 || fc == 6))
		return;
	if (tf == nullptr)
		tf = fopen("/tmp/mame_berr.tsv", "a");
	if (tf == nullptr)
		return;
	uint32_t const a7 = uint32_t(m_maincpu->state_int(M68K_A7));
	uint32_t const sr = uint32_t(m_maincpu->state_int(M68K_SR));
	uint64_t const t_us = uint64_t(machine().time().as_ticks(1000000));
	if (g_berr_want_frame)
	{
		uint16_t const ssw = peek_super16(a7 + 8);
		uint32_t const fa = (uint32_t(peek_super16(a7 + 10)) << 16) | peek_super16(a7 + 12);
		uint16_t const vec = peek_super16(a7 + 6);
		uint32_t const spc = (uint32_t(peek_super16(a7 + 2)) << 16) | peek_super16(a7 + 4);
		fprintf(tf, "%d\t%llu\tFRAME\t%06x\t%04x\t%08x\t%04x\t%08x\t%04x\t%04x\t%04x\tvec=%04x spc=%06x\n",
			-1, (unsigned long long)t_us, logical, unsigned(sr), a7, ssw, fa,
			m_board.regs().genstat, m_board.regs().bsr0, m_board.regs().bsr1,
			vec, spc);
		fflush(tf);
		g_berr_want_frame = false;
	}
	if (g_berr_kpc_left > 0)
	{
		uint16_t const op = peek_super16(logical);
		char const *ev = (op == 0x4e73) ? "RTE" : "KPC";
		fprintf(tf, "%d\t%llu\t%s\t%06x\t%04x\t%08x\t%04x\t%08x\t%04x\t%04x\t%04x\top=%04x\n",
			g_berr_kpc_left, (unsigned long long)t_us, ev, logical, unsigned(sr), a7, op, 0u,
			m_board.regs().genstat, m_board.regs().bsr0, m_board.regs().bsr1, op);
		fflush(tf);
		g_berr_kpc_left--;
		if (op == 0x4e73)
			g_berr_kpc_left = 0;
	}
}
#endif

#if UNIXPC_USER_TRACE
uint32_t unixpc_state::user_crc_logical(uint32_t addr, unsigned nbytes, bool super)
{
	if (nbytes > 4096)
		nbytes = 4096;
	uint8_t buf[4096];
	for (unsigned i = 0; i < nbytes; i++)
	{
		unixpc::probe const q = m_mapper.check(addr + i, false, super, false);
		if (q.result == unixpc::access::ok && q.physical < m_ramsize && m_ramptr != nullptr)
		{
			uint16_t const w = m_ramptr[q.physical >> 1];
			buf[i] = ((addr + i) & 1) ? uint8_t(w) : uint8_t(w >> 8);
		}
		else
			buf[i] = 0xff;
	}
	return unixpc::crc32_ieee(buf, nbytes);
}

void unixpc_state::user_trace(char const *ev, uint32_t va, uint32_t crc, char const *extra)
{
	static FILE *tf;
	static int n;
	if (tf == nullptr)
	{
		tf = fopen("/tmp/mame_user.tsv", "w");
		if (tf != nullptr)
			fprintf(tf, "n\tt_us\tev\tpc\ta0\ta1\ta7\tva\tcrc32\textra\n");
	}
	if (tf == nullptr)
		return;
	uint64_t const t_us = uint64_t(machine().time().as_ticks(1000000));
	uint32_t const pc = unsigned(m_maincpu->pc());
	uint32_t const a0 = uint32_t(m_maincpu->state_int(M68K_A0));
	uint32_t const a1 = uint32_t(m_maincpu->state_int(M68K_A1));
	uint32_t const a7 = uint32_t(m_maincpu->state_int(M68K_A7));
	fprintf(tf, "%d\t%llu\t%s\t%06x\t%08x\t%08x\t%08x\t%06x\t%08x\t%s\n",
		n++, (unsigned long long)t_us, ev, pc, a0, a1, a7, va, crc,
		extra != nullptr ? extra : "-");
	fflush(tf);
}

void unixpc_state::maybe_user_trace(uint16_t fc, uint32_t logical, unixpc::probe const &p, bool write)
{
	(void)write;
	bool const super = (fc == 0) || (fc >= 4);
	if (super)
		return;
	static bool s_entry;
	static bool s_shlib;
	static uint32_t s_pages;
	static int s_nulls;

	if (fc == 1 && logical < 8)
	{
		if (s_nulls++ < 40)
		{
			uint32_t const a7 = uint32_t(m_maincpu->state_int(M68K_A7));
			char extra[80];
			std::snprintf(extra, sizeof(extra), "res=%d stack=%08x",
				int(p.result), user_crc_logical(a7, 32, false));
			user_trace("UNULL", logical, user_crc_logical(a7, 32, false), extra);
		}
		return;
	}

	if ((fc == 2 || fc == 6) && logical >= 0x080000 && logical < 0x300000 && !s_entry)
	{
		s_entry = true;
		uint32_t const a7 = uint32_t(m_maincpu->state_int(M68K_A7));
		uint32_t const scrc = user_crc_logical(a7, 32, false);
		user_trace("UENTRY", logical, scrc, "crt0");
		user_trace("UENV", logical, scrc, "stack32");
	}

	if ((fc == 2 || fc == 6) && logical >= 0x300000 && logical < 0x380000 && !s_shlib)
	{
		s_shlib = true;
		user_trace("SHLIB", logical, user_crc_logical(0x310000, 64, false), "64@310000");
	}

	if (p.result == unixpc::access::ok)
	{
		unsigned const page = unsigned(logical >> 12);
		if (page >= 0x80 && page < 0xa0)
		{
			uint32_t const bit = uint32_t(1) << (page - 0x80);
			if ((s_pages & bit) == 0)
			{
				s_pages |= bit;
				uint32_t const va = uint32_t(page) << 12;
				char extra[24];
				std::snprintf(extra, sizeof(extra), "page=%03x", page);
				user_trace("UPAGE", va, user_crc_logical(va, 4096, false), extra);
			}
		}
	}
}
#endif

uint16_t unixpc_state::ram_mmu_r(offs_t offset)
{
	uint32_t const logical = uint32_t(offset) << 1;
	u16 const fc = m_maincpu->get_fc();
	bool const super = (fc == 0) || (fc >= 4);
	unixpc::probe const p = machine().side_effects_disabled()
		? m_mapper.check(logical, false, super, false)
		: m_mapper.translate(logical, false, super, false);
	if (p.result != unixpc::access::ok)
	{
#if UNIXPC_USER_TRACE
		if (!machine().side_effects_disabled())
			maybe_user_trace(fc, logical, p, false);
#endif
		apply_cpu_fault(p.result, logical, false);
		return 0xffff;
	}
	if (p.physical >= m_ramsize)
		return 0xffff;

#if UNIXPC_USER_TRACE
	if (!machine().side_effects_disabled())
		maybe_user_trace(fc, logical, p, false);
#endif
#if UNIXPC_BERR_TRACE
	if (!machine().side_effects_disabled())
		maybe_berr_kpc(fc, logical);
#endif
	return m_ramptr[p.physical >> 1];
}

void unixpc_state::ram_mmu_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	uint32_t const logical = uint32_t(offset) << 1;
	u16 const fc = m_maincpu->get_fc();
	bool const super = (fc == 0) || (fc >= 4);
	unixpc::probe const p = machine().side_effects_disabled()
		? m_mapper.check(logical, true, super, false)
		: m_mapper.translate(logical, true, super, false);
	if (p.result != unixpc::access::ok)
	{
		apply_cpu_fault(p.result, logical, true);
		return;
	}
	if (p.physical < m_ramsize)
		COMBINE_DATA(&m_ramptr[p.physical >> 1]);
}

uint16_t unixpc_state::bootrom_mirror_r(offs_t offset)
{
	uint16_t const *const rom = &m_bootrom->as_u16(0);
	unsigned const words = unsigned(m_bootrom->bytes() / 2);
	unsigned const live = 0x2000 / 2; // 8K PROM, rest of the 4MB region is unused
	(void)words;
	return rom[offset & (live - 1)];
}

void unixpc_state::ram_phys_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	// ROM overlay: reads come from the PROM, writes go to physical RAM.
	if (m_ramptr != nullptr && (uint32_t(offset) << 1) < m_ramsize)
		COMBINE_DATA(&m_ramptr[offset]);
}

void unixpc_state::unixpc_bind_ui_menu()
{
	// Tab/Ctrl/F12 are either the 3B1 keyboard or stolen by macOS.
	// F10 is not a 3B1 key.
	// Keyboard machines start with UI disabled; only TOGGLE is polled.
	// Tab/Ctrl/F-keys are the 3B1 or macOS. Command+Shift+U (Mac) enables
	// UI; then Tab opens the menu. Esc while UI is on is Back, not 3B1 Esc.
	machine().ioport().set_type_seq(IPT_UI_TOGGLE_UI, 0, SEQ_TYPE_STANDARD,
		input_seq(KEYCODE_LWIN, KEYCODE_LSHIFT, KEYCODE_U,
			input_seq::or_code, KEYCODE_RWIN, KEYCODE_RSHIFT, KEYCODE_U));
	machine().ioport().set_type_seq(IPT_UI_MENU, 0, SEQ_TYPE_STANDARD, input_seq(KEYCODE_TAB));
	machine().ioport().set_type_seq(IPT_UI_RELEASE_POINTER, 0, SEQ_TYPE_STANDARD,
		input_seq(KEYCODE_LWIN, KEYCODE_LSHIFT, KEYCODE_M,
			input_seq::or_code, KEYCODE_RWIN, KEYCODE_RSHIFT, KEYCODE_M));
	logerror("UI_TOGGLE Cmd+Shift+U, UI_MENU Tab, UI_RELEASE_POINTER Cmd+Shift+M\n");
}

void unixpc_state::machine_start()
{
	m_ramptr = m_ram->pointer<uint16_t>();
	m_ramsize = m_ram->size();
	m_mapper.bind(&m_mapram[0]);
	m_board.reset();
	m_dma.reset();
	m_rtc.reset();
	m_kbd.reset();

	m_dma_timer = timer_alloc(FUNC(unixpc_state::dma_tick), this);
	m_dma_timer->adjust(attotime::from_usec(unixpc::DMA_SLICE_USEC), 0,
		attotime::from_usec(unixpc::DMA_SLICE_USEC));

	// 16x 1200 baud keyboard clock into the 6850.
	m_kbd_timer = timer_alloc(FUNC(unixpc_state::kbd_clock), this);
	m_kbd_timer->adjust(attotime::from_hz(1200 * 16), 0, attotime::from_hz(1200 * 16));
	m_hdc_brdy_timer = timer_alloc(FUNC(unixpc_state::hdc_brdy_tick), this);

	save_item(NAME(m_fdc_intrq));
	save_item(NAME(m_hdc_intrq));
	save_item(NAME(m_fdc_drq));
	save_item(NAME(m_hdc_buf));
	save_item(NAME(m_hdc_pos));
	LOG("hdd exists=%d size=%u\n", int(m_hdr0->exists()), unsigned(m_ramsize));

	unixpc_bind_ui_menu();
}

void unixpc_state::machine_reset()
{
	m_board.reset();
	m_dma.reset();
	m_rtc.reset();
	m_kbd.reset();
	m_key_stable = 0;
	m_keys_held = false;
	m_mouse_inited = false;
	m_mouse_pdx = 0;
	m_mouse_pdy = 0;
	m_mouse_sent_b = 0;
	m_dialer_word = 0;
	m_kbd_bits = 0;
	m_kbd_cell = 0;
	m_kbd_rxd = true;
	m_hdc_pos = 0;
	m_hdc_next_lba = ~uint32_t(0);
#if UNIXPC_HDC_TRACE
	m_hdc_trace_n = 0;
#endif
	m_hdc_dma_idx = 0;
	m_hdc_dma_words = 0;
	m_hdc_bdrq = false;
#if UNIXPC_HDC_TRACE
	m_hdc_log_lba = ~uint32_t(0);
#endif
	m_fdc_drq = false;
	m_mcr2_p51 = false;
	m_hdc_head4 = false;
	if (m_hdr0->exists())
	{
		m_hdr0->set_geometry(unixpc::WINCH_CYLS, unixpc::WINCH_HEADS, unixpc::WINCH_SPT);
		LOG("hdd geom %u/%u/%u\n", unixpc::WINCH_CYLS, unixpc::WINCH_HEADS, unixpc::WINCH_SPT);
	}
	m_kbc->write_cts(0);
	m_kbc->write_dcd(0);
	m_maincpu->set_emmu_enable(true);
	unixpc_bind_ui_menu();
	disk_control_w(0);
	m_board.set_clrsint_high(false);
	m_maincpu->set_input_line(M68K_IRQ_6, CLEAR_LINE);
	m_ramromview.select(0);
}

void unixpc_state::error_enable_w(int state)
{
	m_board.set_ee(state != 0);
}

void unixpc_state::parity_enable_w(int state)
{
	m_board.set_pie(state != 0);
}

void unixpc_state::bpplus_w(int state)
{
	LOG("bpplus %d pc=%06x\n", state, unsigned(m_maincpu->pc()));
}

void unixpc_state::vblank_w(int state)
{
	if (!state)
		return;

	{
		static bool once;
		if (!once)
		{
			once = true;
			machine().popmessage("MAME UI: Cmd+Shift+U then Tab  |  uncapture: Cmd+Shift+M");
		}
	}

	// IPT_UI_RELEASE_POINTER is not polled by the core. Pause so the
	// OSD drops capture (should_hide_mouse is false while paused).
	bool const uncap = machine().ioport().type_pressed(IPT_UI_RELEASE_POINTER);
	if (uncap && !m_uncap_held)
	{
		if (machine().paused())
			machine().resume();
		else
			machine().pause();
	}
	m_uncap_held = uncap;

	int const view = int(m_winscale->read() & 3);
	if (view != m_win_view)
	{
		static char const *const view_name[] = {
			"1.5x (1080x522)", "2x (1440x696)", "2.5x (1800x870)", "3x (2160x1044)"
		};
		m_win_view = view;
		render_target *const target = machine().render().first_target();
		if (target != nullptr)
			target->set_view(target->configured_view(view_name[view], 0, 1));
	}

	m_board.vblank();
	bool const irq6 = m_board.timer_latched();
	m_maincpu->set_input_line(M68K_IRQ_6, irq6 ? ASSERT_LINE : CLEAR_LINE);

	if (m_kbd_timer != nullptr && m_kbd_timer->enabled())
		kbd_scan();
}

void unixpc_state::nmi_update()
{
	m_maincpu->set_input_line(M68K_IRQ_7, m_board.nmi_latched() ? ASSERT_LINE : CLEAR_LINE);
}

bool unixpc_state::dma_store_word(uint16_t word)
{
	if (!m_dma.wants_transfer())
		return false;

	uint32_t const logical = m_dma.pointer();
	unixpc::probe const p = m_mapper.translate(logical, true, true, true);
	if (p.result != unixpc::access::ok)
	{
		LOG("dma store fault addr=%06x result=%d\n", logical, int(p.result));
		if (m_board.latch_dma(unixpc::encode_dma_fault(false, logical, m_board.pie())))
			nmi_update();
		return false;
	}
	if (p.physical < m_ramsize)
		m_ramptr[p.physical >> 1] = word;
	m_dma.advance();
	return true;
}

uint16_t unixpc_state::dma_fetch_word()
{
	if (!m_dma.wants_transfer())
		return 0xffff;

	uint32_t const logical = m_dma.pointer();
	unixpc::probe const p = m_mapper.translate(logical, false, true, true);
	if (p.result != unixpc::access::ok)
	{
		if (m_board.latch_dma(unixpc::encode_dma_fault(true, logical, m_board.pie())))
			nmi_update();
		return 0xffff;
	}
	uint16_t const word = (p.physical < m_ramsize) ? m_ramptr[p.physical >> 1] : uint16_t(0xffff);
	m_dma.advance();
	return word;
}

void unixpc_state::dma_service_floppy()
{
	if (!m_fdc_drq)
		return;

	// No media: drain DRQ so the 2797 can complete and interrupt. The ROM
	// then falls through to the Winchester boot path.
	if (!m_floppy->exists())
	{
		if (m_dma.source() == unixpc::dma_source::floppy && m_dma.wants_transfer())
		{
			for (int i = 0; i < 256 && m_fdc_drq; i++)
				(void)m_wd2797->data_r();
		}
		return;
	}

	if (m_dma.source() != unixpc::dma_source::floppy || !m_dma.wants_transfer())
	{
		// Still have to retire DRQ or the 2797 never interrupts.
		if (m_fdc_drq)
			(void)m_wd2797->data_r();
		return;
	}

	if (m_dma.reading())
	{
		uint16_t const word = dma_fetch_word();
		m_wd2797->data_w(uint8_t(word >> 8));
		m_wd2797->data_w(uint8_t(word));
	}
	else
	{
		// One 2797 DRQ is one byte. The board DMA engine is 16-bit, so we
		// pack two successive DRQs into a word.
		uint8_t const b = m_wd2797->data_r();
		if (!m_fdc_have_hi)
		{
			m_fdc_hi = b;
			m_fdc_have_hi = true;
		}
		else
		{
			(void)dma_store_word(uint16_t((uint16_t(m_fdc_hi) << 8) | b));
			m_fdc_have_hi = false;
		}
	}
}

TIMER_CALLBACK_MEMBER(unixpc_state::hdc_brdy_tick)
{
	m_hdc->brdy_w(1);
}

#if UNIXPC_HDC_TRACE
void unixpc_state::hdc_log_burst()
{
	static FILE *tf;
	if (tf == nullptr)
	{
		tf = fopen("/tmp/mame_hdc.tsv", "w");
		if (tf != nullptr)
			fprintf(tf, "n\tdir\tcyl\thead\tsec\tlba\tlen\tcrc32\n");
	}
	if (tf != nullptr && m_hdc_log_lba != ~uint32_t(0))
	{
		fprintf(tf, "%d\t%c\t%u\t%u\t%u\t%u\t512\t%08x\n",
			m_hdc_trace_n, m_dma.reading() ? 'W' : 'R',
			unsigned(m_hdc_cyl), m_hdc_log_head, m_hdc_log_sec,
			m_hdc_log_lba, unixpc::crc32_ieee(m_hdc_buf, 512));
		fflush(tf);
		m_hdc_trace_n++;
	}
}
#endif

void unixpc_state::dma_service_hdc()
{
	if (!m_hdc_bdrq || m_hdc_dma_words <= 0)
		return;

	int budget = unixpc::DMA_SLICE_WORDS;
	while (budget > 0 && m_hdc_dma_words > 0)
	{
		if (m_dma.reading())
		{
			uint16_t const word = dma_fetch_word();
			if (m_hdc_dma_idx + 1 < 512)
			{
				m_hdc_buf[m_hdc_dma_idx] = uint8_t(word >> 8);
				m_hdc_buf[m_hdc_dma_idx + 1] = uint8_t(word);
			}
		}
		else if (m_hdc_dma_idx + 1 < 512)
		{
			(void)dma_store_word(uint16_t((uint16_t(m_hdc_buf[m_hdc_dma_idx]) << 8)
				| m_hdc_buf[m_hdc_dma_idx + 1]));
		}
		m_hdc_dma_idx += 2;
		m_hdc_dma_words--;
		budget--;
	}

	if (m_hdc_dma_words == 0)
	{
#if UNIXPC_HDC_TRACE
		if (m_dma.reading())
			hdc_log_burst();
#endif
		// Last word of this 512: BRDY (count overflow). INTRQ is the
		// controller's job once sector_count hits 0.
		m_hdc->brdy_w(1);
	}
}

TIMER_CALLBACK_MEMBER(unixpc_state::dma_tick)
{
	dma_service_hdc();
	dma_service_floppy();
}

/***************************************************************************
    MISC
***************************************************************************/

uint16_t unixpc_state::gsr_r()
{
	uint16_t g = m_board.regs().genstat;
	if (m_board.pie())
		g |= 0x8400;
	return g;
}

uint16_t unixpc_state::bsr0_r()
{
	return m_board.regs().bsr0;
}

uint16_t unixpc_state::bsr1_r()
{
	return m_board.regs().bsr1;
}

void unixpc_state::csr_w(uint16_t data)
{
	(void)data;
	m_board.clear_status();
	nmi_update();
}

uint16_t unixpc_state::csr_r()
{
	m_board.clear_status();
	nmi_update();
	return 0;
}

void unixpc_state::mcr2_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	// Working 3B1: MCR2 D4 is a PAL latch mirrored on TSR D4. The
	// probe is BSET the shadow, MOVE.W to $E20000, BTST.B #4,$450001,
	// then ANDI the bit off and BTST again (must read 0). HDSEL3 is D0.
	// A full-word store replaces both bits; do not leave D4 stuck at 1.
	(void)offset;
	if (mem_mask & 0x00ff)
	{
		m_hdc_head4 = BIT(data, 0);
		m_mcr2_p51 = BIT(data, 4);
	}
	else if (mem_mask & 0xff00)
	{
		m_hdc_head4 = BIT(data, 8);
		m_mcr2_p51 = BIT(data, 12);
	}
	hdc_apply_head();
	LOG("mcr2 %04x mask=%04x hdsel3=%d p51=%d\n", data, mem_mask, int(m_hdc_head4), int(m_mcr2_p51));
}

uint16_t unixpc_state::modem_r(offs_t offset)
{
	// 882A. RR2 at +0x2000: 0xFF means no modem. Empty I/O is all-ones.
	uint32_t const a = uint32_t(offset) << 1;
	if ((a & 0xf000) == 0x2000)
		return 0x00ff;
	return 0xffff;
}

void unixpc_state::modem_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	(void)offset;
	(void)data;
	(void)mem_mask;
}

void unixpc_state::dialer_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	// 838A: payload on A1-A8 (word offset bits 7-0). A10 loads the low
	// byte; A11 loads the high byte and starts the tone.
	(void)data;
	(void)mem_mask;
	uint8_t const byte = uint8_t(offset & 0xff);
	if (BIT(offset, 9))
		m_dialer_word = uint16_t((m_dialer_word & 0xff00) | byte);
	else if (BIT(offset, 10))
	{
		m_dialer_word = uint16_t((m_dialer_word & 0x00ff) | (uint16_t(byte) << 8));
		bool const on = unixpc::dialer_speaker_on(m_dialer_word);
		uint8_t const tone = uint8_t(m_dialer_word & unixpc::DIALER_TONE_MASK);
		m_beep_lo->set_clock(uint32_t(unixpc::dtmf_row_hz(tone)));
		m_beep_hi->set_clock(uint32_t(unixpc::dtmf_col_hz(tone)));
		m_beep_lo->set_state(on ? 1 : 0);
		m_beep_hi->set_state(on ? 1 : 0);
	}
}

void unixpc_state::tcr_w(offs_t offset, uint16_t data)
{
	m_tcr->write_bit(offset >> 11, BIT(data, 14));
}

uint16_t unixpc_state::tsr_r()
{
	// b3..b0 active-low: msg waiting, ring2, ring1, offhook. Idle = all 1s.
	// b4 is the P5.1 PAL feedback (MCR2 D4, sticky until RESET).
	// Phone bits on D0-D7; duplicate D4 into D12 so an even-byte
	// MOVE.B of $450000 still sees the PAL (D8-D15).
	uint16_t data = 0x000f;
	if (m_mcr2_p51)
		data |= 0x1010;
	return data;
}

uint16_t unixpc_state::rtc_r()
{
	return m_rtc.read_word();
}

void unixpc_state::rtc_w(uint16_t data)
{
	m_rtc.write_control(data);
}

uint16_t unixpc_state::line_printer_r()
{
	uint16_t data = 0;

	data |= 1; // no dial tone detected
	data |= 1 << 1; // no parity error
	data |= m_hdc_intrq ? 1<<2 : 0<<2;
	// ROM polls this bit, not the 68010 IRQ2 pin. Latch until FDC status is read.
	data |= (m_fdc_intrq || m_fdc_irq_latch) ? 1<<3 : 0;



	return data;
}

/***************************************************************************
    DMA
***************************************************************************/

uint16_t unixpc_state::diskdma_size_r()
{
	// PROM busy-waits until this register differs from the value it
	// loaded (two's complement word count | DMAEN). Echo enable/dir as
	// written; forcing bits 15-14 high made a 512-byte load look like it
	// never started and hung the wait.
	uint16_t data = uint16_t(m_dma.count() & 0x3fff);
	if (m_dma.enabled())
		data |= 0x8000;
	if (m_dma.reading())
		data |= 0x4000;
	return data;
}

void unixpc_state::diskdma_size_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	(void)offset;
	uint16_t value = m_dma.count();
	COMBINE_DATA(&value);
	// 14-bit up-counter. PROM loads two's complement of (wordcount+1); a
	// 512-byte sector is 0xFEFF so bits 13-0 start at 0x3EFF and 256
	// increments land on 0x3FFF (done). Do not add 1 here or the last
	// word is dropped.
	m_dma.set_count(uint16_t(value & 0x3fff));
	m_dma.set_enabled(BIT(value, 15));
	m_dma.set_reading(BIT(value, 14));
	LOG("dma count=%04x en=%d rd=%d ptr=%06x src=%d\n",
		m_dma.count(), int(m_dma.enabled()), int(m_dma.reading()), m_dma.pointer(), int(m_dma.source()));
	if (m_dma.wants_transfer())
		dma_service_floppy();
}

void unixpc_state::diskdma_ptr_w(offs_t offset, uint16_t data)
{
	(void)data;
	// A14 of the access, not the written data, selects high vs low half.
	uint32_t const addr = uint32_t(offset) << 1;
	if (addr & 0x4000)
		m_dma.set_pointer_high(addr);
	else
		m_dma.set_pointer_low(addr);
}

/***************************************************************************
    FLOPPY
***************************************************************************/

void unixpc_state::disk_control_w(uint8_t data)
{
	LOG("disk_control %02x\n", data);
	m_hdc_head_low = uint8_t(data & 7);
	hdc_apply_head();

	bool const hd_sel = BIT(data, 3);
	bool const fd_sel = BIT(data, 6);

	if (!BIT(data, 4))
		m_hdc->reset();

	// DRDY is a drive pin, not the select bit. Apply after reset so a
	// kernel master-reset pulse does not leave READY clear.
	m_hdc->drdy_w(m_hdr0->exists());
	m_hdc->sc_w(1);

	m_floppy->mon_w(!BIT(data, 5));

	if (fd_sel && m_floppy->exists())
		m_wd2797->set_floppy(m_floppy);
	else
		m_wd2797->set_floppy(nullptr);

	// Rising MR* starts a 2797 restore that never finishes without media.
	if (m_floppy->exists())
		m_wd2797->mr_w(BIT(data, 7));

	if (hd_sel && !fd_sel)
		m_dma.set_source(unixpc::dma_source::hard_disk);
	else if (fd_sel && !hd_sel)
		m_dma.set_source(unixpc::dma_source::floppy);
	else if (hd_sel)
		m_dma.set_source(unixpc::dma_source::hard_disk);
	else if (fd_sel)
		m_dma.set_source(unixpc::dma_source::floppy);
	else
		m_dma.set_source(unixpc::dma_source::none);
}

void unixpc_state::wd2797_intrq_w(int state)
{
	if (state != m_fdc_intrq)
		LOG("fdc irq %d\n", state);
	m_fdc_intrq = state != 0;
	if (m_fdc_intrq)
		m_fdc_irq_latch = true;
	m_int02->in_w<1>(state);
}

uint8_t unixpc_state::fdc_r(offs_t offset)
{
	// Empty drive: PROM must see NOT READY so it fails floppy and opens the HD.
	if (!m_floppy->exists() && (offset & 3) == 0)
	{
		m_fdc_irq_latch = false;
		m_fdc_intrq = false;
		m_int02->in_w<1>(0);
		return 0x80;
	}

	uint8_t const v = m_wd2797->read(offset);
	if ((offset & 3) == 0)
		m_fdc_irq_latch = false;
	return v;
}

void unixpc_state::fdc_w(offs_t offset, uint8_t data)
{
	// Any command with no media completes immediately: INTRQ, no DRQ, NOT READY.
	if (!m_floppy->exists() && (offset & 3) == 0)
	{
		// PROM polls LPSR bit 3. Never raise 68010 IRQ2: the kernel FD
		// driver treats a pending FDC INTRQ at iinit as spurious.
		m_fdc_intrq = true;
		m_fdc_irq_latch = true;
		m_int02->in_w<1>(0);
		return;
	}
	m_wd2797->write(offset, data);
}

uint16_t unixpc_state::fdc16_r(offs_t offset, uint16_t mem_mask)
{
	(void)mem_mask;
	// PROM helpers form E10000 + 2*reg and MOVE.W. A separate .w() range
	// overlapping this .r() left 0xE10000-0xE10007 unmapped for reads, so
	// the ready-poll saw 0 (READY) on an empty drive and "booted" floppy.
	uint8_t const v = fdc_r(offset & 3);
	return uint16_t(v) | (uint16_t(v) << 8);
}

void unixpc_state::fdc16_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	uint8_t const v = (mem_mask & 0x00ff) ? uint8_t(data) : uint8_t(data >> 8);
	fdc_w(offset & 3, v);
}

void unixpc_state::wd2797_drq_w(int state)
{
	if ((state != 0) != m_fdc_drq)
		LOG("fdc drq %d\n", state);
	m_fdc_drq = state != 0;
	if (m_fdc_drq)
		dma_service_floppy();
}

void unixpc_state::wd1010_intrq_w(int state)
{
	m_hdc_intrq = state;
	m_int02->in_w<0>(state);
}

void unixpc_state::hdc_apply_head()
{
	m_hdc->head_w((m_hdc_head_low & 7) | (m_hdc_head4 ? 8 : 0));
}

void unixpc_state::hdc_reg_w(offs_t offset, uint8_t data)
{
	unsigned const reg = offset & 7;
	if (reg == 7)
	{
		// Sample CHS + HDSEL3 now. Later MCR2/SDH writes must not
		// retarget the in-flight 512-byte bursts (1K = LBA, LBA+1).
		hdc_apply_head();
		unsigned const head = unsigned(m_hdc_head_low & 7) | (m_hdc_head4 ? 8u : 0u);
		m_hdc_next_lba = unixpc::winchester_lba(m_hdc_cyl, head, m_hdc_sector & 0x1f);
		m_hdc->brdy_w(0);
	}
	if (reg == 2)
		m_hdc_seccnt = data;
	if (reg == 3)
	{
		m_hdc_sector = data;
		hdc_apply_head();
	}
	if (reg == 4)
		m_hdc_cyl = uint16_t((m_hdc_cyl & 0xff00) | data);
	if (reg == 5)
	{
		// WD2010-class ASIC (not a drive model): 11-bit cylinder, extra
		// on-chip ECC unused by 3.51, higher step rates. Probe is cyl-high
		// width (bit 2 sticks). 3B1 kernels still cap ~1400 cyl. DRUN is
		// analog 74123/R63 glue on some boards, not a task-file bit.
		data &= 0x07;
		m_hdc_cyl = uint16_t((m_hdc_cyl & 0x00ff) | (uint16_t(data) << 8));
	}
	if (reg == 6)
	{
		m_hdc_head_low = uint8_t(data & 7);
		hdc_apply_head();
		// One Winchester; SDH drive-select bits must still find drive 0.
		data &= ~0x18;
	}
#if UNIXPC_EXTRA_LOG
	if (reg == 7)
	{
		unsigned const head = unsigned(m_hdc_head_low & 7) | (m_hdc_head4 ? 8u : 0u);
		uint8_t const sec = uint8_t(m_hdc_sector & 0x1f);
		LOG("hdc cmd %02x pc=%06x chs=%u:%u:%u nsec=%u lba=%u ptr=%06x cnt=%04x\n",
			data, unsigned(m_maincpu->pc()), unsigned(m_hdc_cyl), head, unsigned(sec),
			unsigned(m_hdc_seccnt), unixpc::winchester_lba(m_hdc_cyl, head, sec),
			m_dma.pointer(), m_dma.count());
	}
#endif
	// Sector number is 5 bits. Bit 5 is an HDSEL3 alias, not LBA.
	if (reg == 3)
		data &= 0x1f;
	m_hdc->write(offset, data);
}

uint8_t unixpc_state::hdc_reg_r(offs_t offset)
{
	uint8_t v = m_hdc->read(offset);
	if ((offset & 7) == 5)
		v = uint8_t((m_hdc_cyl >> 8) & 0x07);
	return v;
}

void unixpc_state::hdc_word_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	uint8_t const v = (mem_mask & 0x00ff) ? uint8_t(data) : uint8_t(data >> 8);
	hdc_reg_w(offset & 7, v);
}

uint16_t unixpc_state::hdc_word_r(offs_t offset, uint16_t mem_mask)
{
	(void)mem_mask;
	uint8_t const v = hdc_reg_r(offset & 7);
	return uint16_t(v) | (uint16_t(v) << 8);
}

void unixpc_state::hdc_bcr_w(int state)
{
	if (state)
		m_hdc_pos = 0;
}

void unixpc_state::hdc_out_data(uint8_t data)
{
	if (m_hdc_pos < 512)
		m_hdc_buf[m_hdc_pos++] = data;
}

uint8_t unixpc_state::hdc_in_data()
{
	if (m_hdc_pos < 512)
		return m_hdc_buf[m_hdc_pos++];
	return 0xff;
}

void unixpc_state::hdc_bdrq_w(int state)
{
	if (!state)
	{
		m_hdc_brdy_timer->adjust(attotime::never);
		m_hdc_bdrq = false;
		m_hdc->brdy_w(0);
		return;
	}

	// Label geometry 1400/16/17, 0-based sector. Head bit 3 is MCR2
	// (HDSEL3). Sector bit 5 is not a head line.
	if (m_hdr0->exists())
		m_hdr0->set_geometry(unixpc::WINCH_CYLS, unixpc::WINCH_HEADS, unixpc::WINCH_SPT);
	unsigned const head = unsigned(m_hdc_head_low & 7) | (m_hdc_head4 ? 8u : 0u);
	uint8_t const sec = uint8_t(m_hdc_sector & 0x1f);
	uint32_t lba = m_hdc_next_lba;
	if (lba == ~uint32_t(0))
		lba = unixpc::winchester_lba(m_hdc_cyl, head, sec);

	LOG("hdc bdrq lba=%u chs=%u:%u:%u dma=%04x rd=%d\n",
		lba, unsigned(m_hdc_cyl), unsigned(head), unsigned(sec),
		m_dma.count(), int(m_dma.reading()));

	// One BDRQ is one 512-byte sector. Arm a 256-word DMA drain at
	// 1 MHz (20 words / 20 µs). Do not copy the sector here: an instant
	// fill plus a long BRDY delay lets the CPU start the next bio
	// (often a write) while a 1K read is still in flight. 1K (DMA
	// 0x3DFF) is two of these bursts; WD bit2 + sector_count=2 raises
	// BDRQ again. Do not drain the whole DMA on the first BDRQ.
#if UNIXPC_HDC_TRACE
	m_hdc_log_lba = lba;
	m_hdc_log_head = head;
	m_hdc_log_sec = unsigned(sec);
#endif
	m_hdc_dma_idx = 0;
	m_hdc_dma_words = unixpc::SECTOR_WORDS;
	m_hdc_bdrq = true;
	m_hdc_pos = 0;
	if (!m_dma.reading())
	{
		if (m_hdr0->exists() && lba != ~uint32_t(0) && lba < unixpc::WINCH_SECTORS)
			(void)m_hdr0->read(lba, m_hdc_buf);
		else
			std::memset(m_hdc_buf, 0, sizeof(m_hdc_buf));
#if UNIXPC_HDC_TRACE
		hdc_log_burst();
#endif
	}
	m_hdc_next_lba = unixpc::winchester_lba_next(lba);

	if (!m_dma.wants_transfer())
	{
		// DMA miss: still pulse BRDY so CIP can finish.
		m_hdc_dma_words = 0;
		m_hdc->brdy_w(1);
		return;
	}

	m_hdc->brdy_w(0);
}

void unixpc_state::kbd_scan()
{
	uint32_t now[4];
	for (int bank = 0; bank < 4; bank++)
		now[bank] = m_keys[bank].read_safe(0);

	// Two identical vblank samples before a report: host OS key-repeat
	// and 1-frame ioport glitches otherwise look like extra makes.
	bool same_as_seen = true;
	for (int bank = 0; bank < 4; bank++)
		if (now[bank] != m_key_seen[bank])
			same_as_seen = false;
	if (!same_as_seen)
	{
		for (int bank = 0; bank < 4; bank++)
			m_key_seen[bank] = now[bank];
		m_key_stable = 0;
	}
	else if (m_key_stable < 1)
		m_key_stable++;
	else
	{
		bool changed = false;
		for (int bank = 0; bank < 4; bank++)
			if (now[bank] != m_key_down[bank])
				changed = true;
		if (changed)
		{
			m_kbd.begin_keyboard_if_needed();
			uint8_t snap[16];
			int const n = unixpc::kbd_queue::fill_snapshot(now, snap, 16);
			if (n > 0)
			{
				for (int i = 0; i < n; i++)
					m_kbd.push(snap[i]);
			}
			else if (m_keys_held)
				m_kbd.push(unixpc::kbd_queue::ALL_KEYS_UP);
			for (int bank = 0; bank < 4; bank++)
				m_key_down[bank] = now[bank];
			m_keys_held = unixpc::kbd_queue::any_down(now);
		}
	}

	s16 const sx = s16(m_mouse_x->read());
	s16 const sy = s16(m_mouse_y->read());
	uint8_t const mb = uint8_t(m_mouse_b->read() & 0x07);
	if (!m_mouse_inited)
	{
		m_mouse_lx = sx;
		m_mouse_ly = sy;
		m_mouse_lb = mb;
		m_mouse_inited = true;
	}
	else
	{
		m_mouse_pdx += int(sx) - m_mouse_lx;
		m_mouse_pdy += int(sy) - m_mouse_ly;
		m_mouse_lx = sx;
		m_mouse_ly = sy;
		m_mouse_lb = mb;
		// 1200 baud ≈ one 4-byte mouse packet per 37 ms. Queueing a
		// packet every vblank overruns the 6850 and the pointer keeps
		// moving after the hand stops. Coalesce until the UART is idle.
		if (m_kbd.empty() && m_kbd_bits == 0
			&& (m_mouse_pdx != 0 || m_mouse_pdy != 0 || mb != m_mouse_sent_b))
		{
			int const acc = int(m_mouse_acc->read());
			int const sdx = unixpc::kbd_queue::scale_delta(m_mouse_pdx, acc);
			int const sdy = unixpc::kbd_queue::scale_delta(m_mouse_pdy, acc);
			uint8_t pkt[5];
			int const n = unixpc::kbd_queue::fill_mouse(sdx, sdy, mb, pkt);
			for (int i = 0; i < n; i++)
				m_kbd.push(pkt[i]);
			m_kbd.mark_mouse();
			m_mouse_pdx -= sdx;
			m_mouse_pdy -= sdy;
			m_mouse_sent_b = mb;
		}
	}
}

void unixpc_state::kbd_shift_bit()
{
	if (m_kbd_bits == 0)
	{
		if (m_kbd.empty())
		{
			m_kbd_rxd = true;
			m_kbc->write_rxd(1);
			return;
		}
		uint8_t const b = m_kbd.pop();
		// start bit 0, 8 data LSB first, two stop bits 1
		m_kbd_shift = uint16_t(b) << 1 | 0x600;
		m_kbd_bits = 11;
	}

	m_kbd_rxd = BIT(m_kbd_shift, 0);
	m_kbc->write_rxd(m_kbd_rxd);
	m_kbd_shift >>= 1;
	m_kbd_bits--;
}

TIMER_CALLBACK_MEMBER(unixpc_state::kbd_clock)
{
	// 6850 CR is ÷16: one bit cell is 16 RxC edges at 19.2 kHz (1200 baud).
	// One edge per bit looked like 19200 baud and RDRF never set.
	if (m_kbd_cell == 0)
		kbd_shift_bit();
	m_kbc->write_rxc(1);
	m_kbc->write_txc(1);
	m_kbc->write_rxc(0);
	m_kbc->write_txc(0);
	m_kbd_cell = (m_kbd_cell + 1) & 15;
}

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


/***************************************************************************
    ADDRESS MAPS
***************************************************************************/

void unixpc_state::unixpc_mem(address_map &map)
{
	map(0x000000, 0x3fffff).view(m_ramromview);
	m_ramromview[0](0x000000, 0x3fffff).rw(FUNC(unixpc_state::bootrom_mirror_r), FUNC(unixpc_state::ram_phys_w));
	m_ramromview[1](0x000000, 0x3fffff).rw(FUNC(unixpc_state::ram_mmu_r), FUNC(unixpc_state::ram_mmu_w));
	// Zone A is decoded on A19-A16 only; the rest of each 64K block mirrors.
	map(0x400000, 0x4007ff).mirror(0x00f800).rw(FUNC(unixpc_state::mapram_r), FUNC(unixpc_state::mapram_w)).share("mapram");
	map(0x410000, 0x410001).mirror(0x00fffe).r(FUNC(unixpc_state::gsr_r));
	map(0x420000, 0x427fff).ram().share("videoram");
	map(0x430000, 0x430001).mirror(0x00fffe).r(FUNC(unixpc_state::bsr0_r));
	map(0x440000, 0x440001).mirror(0x00fffe).r(FUNC(unixpc_state::bsr1_r));
	map(0x450000, 0x450001).mirror(0x00fffe).r(FUNC(unixpc_state::tsr_r));
	map(0x460000, 0x460001).mirror(0x00fffe).rw(FUNC(unixpc_state::diskdma_size_r), FUNC(unixpc_state::diskdma_size_w));
	map(0x470000, 0x470001).mirror(0x00fffe).r(FUNC(unixpc_state::line_printer_r));
	map(0x480000, 0x480001).mirror(0x00fffe).w(FUNC(unixpc_state::rtc_w));
	map(0x490000, 0x490001).select(0x7000).w(FUNC(unixpc_state::tcr_w));
	// MCR is 16-bit; CLRSINT- is D15 on the even byte (D8-D15).
	map(0x4a0000, 0x4a0001).w("mreg", FUNC(output_latch_device::write)).umask16(0xff00);
	map(0x4b0000, 0x4b0fff).w(FUNC(unixpc_state::dialer_w));
	map(0x4c0000, 0x4c0001).mirror(0x00fffe).rw(FUNC(unixpc_state::csr_r), FUNC(unixpc_state::csr_w));
	map(0x4d0000, 0x4d7fff).w(FUNC(unixpc_state::diskdma_ptr_w));
	map(0x4e0001, 0x4e0001).w(FUNC(unixpc_state::disk_control_w)).cswidth(16);
	map(0x4f0001, 0x4f0001).w("printlatch", FUNC(output_latch_device::write));
	// 16K PROM at 0x800000 only. The skeleton mirrored it through
	// 0xFFFFFF (mirror 0x7FC000), which ate FDC/HDC/KBC reads: writes
	// still reached the chips (ROM is read-only) but MOVE.W (a0) of
	// status returned PROM bytes and the floppy poll saw READY.
	map(0x800000, 0x803fff).rom().region("bootrom", 0);
	map(0xe00000, 0xe0000f).mirror(0x0fff0).rw(FUNC(unixpc_state::hdc_word_r), FUNC(unixpc_state::hdc_word_w));
	map(0xe10000, 0xe10007).mirror(0x0fff8).rw(FUNC(unixpc_state::fdc16_r), FUNC(unixpc_state::fdc16_w));
	// Whole 64K block, and the $F2xxxx alias. Byte or word writes.
	map(0xe20000, 0xe20001).mirror(0x00fffe).w(FUNC(unixpc_state::mcr2_w));
	map(0xf20000, 0xf20001).mirror(0x00fffe).w(FUNC(unixpc_state::mcr2_w));
	map(0xe30000, 0xe30001).r(FUNC(unixpc_state::rtc_r));
	map(0xe40000, 0xe40001).select(0x7000).w(FUNC(unixpc_state::gcr_w));
	map(0xe50000, 0xe50007).rw("mpsc", FUNC(upd7201_device::cd_ba_r), FUNC(upd7201_device::cd_ba_w)).umask16(0x00ff);
	map(0xe60000, 0xe6ffff).rw(FUNC(unixpc_state::modem_r), FUNC(unixpc_state::modem_w));
	map(0xe70000, 0xe70003).rw("kbc", FUNC(acia6850_device::read), FUNC(acia6850_device::write)).umask16(0xff00);
}

/***************************************************************************
    INPUT PORTS
***************************************************************************/

static INPUT_PORTS_START( unixpc )
	PORT_START("KEY0")
	PORT_BIT(0x00000002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Up") PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x00000800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Down") PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x00004000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x00008000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x00002000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RETURN") PORT_CODE(KEYCODE_ENTER) PORT_CHAR(13)
	PORT_BIT(0x00000100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Backspace") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)
	PORT_BIT(0x00000200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Tab") PORT_CODE(KEYCODE_TAB) PORT_CHAR(9)
	PORT_BIT(0x00200000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F1") PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x00400000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F2") PORT_CODE(KEYCODE_F2)
	PORT_BIT(0x00800000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F3") PORT_CODE(KEYCODE_F3)
	PORT_BIT(0x01000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F4") PORT_CODE(KEYCODE_F4)
	PORT_BIT(0x02000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F5") PORT_CODE(KEYCODE_F5)
	PORT_BIT(0x04000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F6") PORT_CODE(KEYCODE_F6)
	PORT_BIT(0x08000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Esc") PORT_CODE(KEYCODE_ESC) PORT_CHAR(27)
	PORT_BIT(0x10000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F7") PORT_CODE(KEYCODE_F7)
	PORT_BIT(0x20000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F8") PORT_CODE(KEYCODE_F8)

	PORT_START("KEY1")
	PORT_BIT(0x00000001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
	PORT_BIT(0x00000080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_QUOTE) PORT_CHAR('\'') PORT_CHAR('"')
	PORT_BIT(0x00000400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Help") PORT_CODE(KEYCODE_PGUP)
	PORT_BIT(0x00001000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_COMMA) PORT_CHAR(',') PORT_CHAR('<')
	PORT_BIT(0x00002000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_MINUS) PORT_CHAR('-') PORT_CHAR('_')
	PORT_BIT(0x00004000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP) PORT_CHAR('.') PORT_CHAR('>')
	PORT_BIT(0x00008000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/') PORT_CHAR('?')
	PORT_BIT(0x00010000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_0) PORT_CHAR('0') PORT_CHAR(')')
	PORT_BIT(0x00020000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_1) PORT_CHAR('1') PORT_CHAR('!')
	PORT_BIT(0x00040000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_2) PORT_CHAR('2') PORT_CHAR('@')
	PORT_BIT(0x00080000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_3) PORT_CHAR('3') PORT_CHAR('#')
	PORT_BIT(0x00100000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_4) PORT_CHAR('4') PORT_CHAR('$')
	PORT_BIT(0x00200000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_5) PORT_CHAR('5') PORT_CHAR('%')
	PORT_BIT(0x00400000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_6) PORT_CHAR('6') PORT_CHAR('^')
	PORT_BIT(0x00800000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_7) PORT_CHAR('7') PORT_CHAR('&')
	PORT_BIT(0x01000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_8) PORT_CHAR('8') PORT_CHAR('*')
	PORT_BIT(0x02000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_9) PORT_CHAR('9') PORT_CHAR('(')
	PORT_BIT(0x08000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_COLON) PORT_CHAR(';') PORT_CHAR(':')
	PORT_BIT(0x20000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_EQUALS) PORT_CHAR('=') PORT_CHAR('+')

	PORT_START("KEY2")
	PORT_BIT(0x00000002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Cmd") PORT_CODE(KEYCODE_LALT) PORT_CODE(KEYCODE_RALT)
	PORT_BIT(0x00000020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Caps Lock") PORT_CODE(KEYCODE_CAPSLOCK)
	PORT_BIT(0x00000100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Shift") PORT_CODE(KEYCODE_LSHIFT)
	PORT_BIT(0x00000200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Right Shift") PORT_CODE(KEYCODE_RSHIFT)
	PORT_BIT(0x00001000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Ctrl") PORT_CODE(KEYCODE_LCONTROL)
	PORT_BIT(0x00002000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Right Ctrl") PORT_CODE(KEYCODE_RCONTROL)
	PORT_BIT(0x08000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('[') PORT_CHAR('{')
	PORT_BIT(0x10000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR('\\') PORT_CHAR('|')
	PORT_BIT(0x20000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR(']') PORT_CHAR('}')

	PORT_START("KEY3")
	PORT_BIT(0x00000001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_TILDE) PORT_CHAR('`') PORT_CHAR('~')
	PORT_BIT(0x00000002, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_A) PORT_CHAR('a') PORT_CHAR('A')
	PORT_BIT(0x00000004, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_B) PORT_CHAR('b') PORT_CHAR('B')
	PORT_BIT(0x00000008, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_C) PORT_CHAR('c') PORT_CHAR('C')
	PORT_BIT(0x00000010, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_D) PORT_CHAR('d') PORT_CHAR('D')
	PORT_BIT(0x00000020, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_E) PORT_CHAR('e') PORT_CHAR('E')
	PORT_BIT(0x00000040, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_F) PORT_CHAR('f') PORT_CHAR('F')
	PORT_BIT(0x00000080, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_G) PORT_CHAR('g') PORT_CHAR('G')
	PORT_BIT(0x00000100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_H) PORT_CHAR('h') PORT_CHAR('H')
	PORT_BIT(0x00000200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_I) PORT_CHAR('i') PORT_CHAR('I')
	PORT_BIT(0x00000400, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_J) PORT_CHAR('j') PORT_CHAR('J')
	PORT_BIT(0x00000800, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_K) PORT_CHAR('k') PORT_CHAR('K')
	PORT_BIT(0x00001000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_L) PORT_CHAR('l') PORT_CHAR('L')
	PORT_BIT(0x00002000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_M) PORT_CHAR('m') PORT_CHAR('M')
	PORT_BIT(0x00004000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_N) PORT_CHAR('n') PORT_CHAR('N')
	PORT_BIT(0x00008000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_O) PORT_CHAR('o') PORT_CHAR('O')
	PORT_BIT(0x00010000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_P) PORT_CHAR('p') PORT_CHAR('P')
	PORT_BIT(0x00020000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_Q) PORT_CHAR('q') PORT_CHAR('Q')
	PORT_BIT(0x00040000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_R) PORT_CHAR('r') PORT_CHAR('R')
	PORT_BIT(0x00080000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_S) PORT_CHAR('s') PORT_CHAR('S')
	PORT_BIT(0x00100000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_T) PORT_CHAR('t') PORT_CHAR('T')
	PORT_BIT(0x00200000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_U) PORT_CHAR('u') PORT_CHAR('U')
	PORT_BIT(0x00400000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_V) PORT_CHAR('v') PORT_CHAR('V')
	PORT_BIT(0x00800000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_W) PORT_CHAR('w') PORT_CHAR('W')
	PORT_BIT(0x01000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_X) PORT_CHAR('x') PORT_CHAR('X')
	PORT_BIT(0x02000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_Y) PORT_CHAR('y') PORT_CHAR('Y')
	PORT_BIT(0x04000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z) PORT_CHAR('z') PORT_CHAR('Z')
	PORT_BIT(0x80000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Delete") PORT_CODE(KEYCODE_DEL)

	PORT_START("PHOSPHOR")
	PORT_CONFNAME(0x03, 0x00, "Phosphor")
	PORT_CONFSETTING(0x00, "White")
	PORT_CONFSETTING(0x01, "Green")
	PORT_CONFSETTING(0x02, "Amber")
	PORT_START("WINSCALE")
	PORT_CONFNAME(0x03, 0x01, "Window scale")
	PORT_CONFSETTING(0x00, "1.5x")
	PORT_CONFSETTING(0x01, "2x")
	PORT_CONFSETTING(0x02, "2.5x")
	PORT_CONFSETTING(0x03, "3x")
	PORT_START("MOUSEACC")
	PORT_ADJUSTER(100, "Mouse acceleration") PORT_MINMAX(50, 300)

	PORT_START("MOUSEX")
	PORT_BIT(0xffff, 0x0000, IPT_MOUSE_X) PORT_SENSITIVITY(40) PORT_KEYDELTA(0)
	PORT_START("MOUSEY")
	PORT_BIT(0xffff, 0x0000, IPT_MOUSE_Y) PORT_SENSITIVITY(40) PORT_KEYDELTA(0)
	PORT_START("MOUSEB")
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_BUTTON1) PORT_CODE(MOUSECODE_BUTTON1) PORT_NAME("Mouse Left")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_BUTTON2) PORT_CODE(MOUSECODE_BUTTON2) PORT_NAME("Mouse Right")
INPUT_PORTS_END


/***************************************************************************
    MACHINE DRIVERS
***************************************************************************/

static void unixpc_floppies(device_slot_interface &device)
{
	device.option_add("525dd", FLOPPY_525_DD);
}

void unixpc_state::unixpc(machine_config &config)
{
	// basic machine hardware
	M68010(config, m_maincpu, 40_MHz_XTAL / 4);
	m_maincpu->set_addrmap(AS_PROGRAM, &unixpc_state::unixpc_mem);
	m_maincpu->set_emmu_enable(true);

	LS259(config, m_gcr); // 7K
	m_gcr->q_out_cb<0>().set(FUNC(unixpc_state::error_enable_w));
	m_gcr->q_out_cb<1>().set(FUNC(unixpc_state::parity_enable_w));
	m_gcr->q_out_cb<2>().set(FUNC(unixpc_state::bpplus_w));
	m_gcr->q_out_cb<3>().set(FUNC(unixpc_state::romlmap_w));

	LS259(config, m_tcr); // 10K

	INPUT_MERGER_ANY_HIGH(config, m_int02); // 26H pins 3-6
	m_int02->output_handler().set_inputline(m_maincpu, M68K_IRQ_2);

	output_latch_device &mreg(OUTPUT_LATCH(config, "mreg"));
	mreg.bit_handler<0>().set_output("led_0").invert();
	mreg.bit_handler<1>().set_output("led_1").invert();
	mreg.bit_handler<2>().set_output("led_2").invert();
	mreg.bit_handler<3>().set_output("led_3").invert();
	// bit 4 (D12) = 0 = modem baud rate from UART clock inputs, 1 = baud from programmable timer
	mreg.bit_handler<5>().set("printer", FUNC(centronics_device::write_strobe)).invert();
	// D14: 1 = DMA reads memory (device write), 0 = DMA writes memory
	mreg.bit_handler<6>().set([this](int state) { m_dma.set_reading(state != 0); });
	// D15: CLRSINT-. High releases the 60 Hz latch; low holds it clear.
	mreg.bit_handler<7>().set([this](int state) {
		LOG("clrsint_high %d\n", state);
		m_board.set_clrsint_high(state != 0);
		if (!state)
			m_maincpu->set_input_line(M68K_IRQ_6, CLEAR_LINE);
	});

	// video hardware
	screen_device &screen(SCREEN(config, "screen"));
	screen.set_screen_update(FUNC(unixpc_state::screen_update));
	screen.set_raw(40_MHz_XTAL / 2, 896, 0, unixpc::SCREEN_W, 367, 0, unixpc::SCREEN_H);
	screen.set_physical_aspect(unixpc::SCREEN_W, unixpc::SCREEN_H);
	screen.set_palette("palette");
	screen.screen_vblank().set(FUNC(unixpc_state::vblank_w));
	// vsync should actually last 17264 pixels

	config.set_default_layout(layout_unixpc);

	PALETTE(config, "palette", palette_device::MONOCHROME);

	SPEAKER(config, "mono").front_center();
	BEEP(config, m_beep_lo, 697).add_route(ALL_OUTPUTS, "mono", 0.20);
	BEEP(config, m_beep_hi, 1209).add_route(ALL_OUTPUTS, "mono", 0.20);

	// internal ram
	RAM(config, RAM_TAG).set_default_size("4M").set_extra_options("512K,1M,2M,4M");

	// floppy
	WD2797(config, m_wd2797, 40_MHz_XTAL / 40); // 1PCK (CPU clock) divided by custom DMA chip
	m_wd2797->intrq_wr_callback().set(FUNC(unixpc_state::wd2797_intrq_w));
	m_wd2797->drq_wr_callback().set(FUNC(unixpc_state::wd2797_drq_w));
	FLOPPY_CONNECTOR(config, "wd2797:0", unixpc_floppies, "525dd", floppy_image_device::default_mfm_floppy_formats);

	WD1010(config, m_hdc, 40_MHz_XTAL / 8);
	m_hdc->out_intrq_callback().set(FUNC(unixpc_state::wd1010_intrq_w));
	m_hdc->out_bdrq_callback().set(FUNC(unixpc_state::hdc_bdrq_w));
	m_hdc->out_bcr_callback().set(FUNC(unixpc_state::hdc_bcr_w));
	m_hdc->out_data_callback().set(FUNC(unixpc_state::hdc_out_data));
	m_hdc->in_data_callback().set(FUNC(unixpc_state::hdc_in_data));
	HARDDISK(config, m_hdr0, 0);

	upd7201_device &mpsc(UPD7201(config, "mpsc", 19.6608_MHz_XTAL / 8));
	mpsc.out_txda_callback().set("rs232", FUNC(rs232_port_device::write_txd));
	mpsc.out_dtra_callback().set("rs232", FUNC(rs232_port_device::write_dtr));
	mpsc.out_rtsa_callback().set("rs232", FUNC(rs232_port_device::write_rts));
	// IRQ4 is connected after the PROM installs vectors; a 7201 reset IRQ
	// otherwise takes autovector 0x70 while 0x70 still contains 0x00000070.

	ACIA6850(config, m_kbc);
	m_kbc->irq_handler().set_inputline(m_maincpu, M68K_IRQ_3);

	// TODO: RTC
	//TC8250(config, "rtc", 32.768_kHz_XTAL);

	rs232_port_device &rs232(RS232_PORT(config, "rs232", default_rs232_devices, nullptr));
	rs232.rxd_handler().set("mpsc", FUNC(upd7201_device::rxa_w));
	rs232.dsr_handler().set("mpsc", FUNC(upd7201_device::dcda_w));
	rs232.cts_handler().set("mpsc", FUNC(upd7201_device::ctsa_w));

	centronics_device &printer(CENTRONICS(config, "printer", centronics_devices, nullptr));
	output_latch_device &printlatch(OUTPUT_LATCH(config, "printlatch"));
	printer.set_output_latch(printlatch);
}


/***************************************************************************
    ROM DEFINITIONS
***************************************************************************/

// ROMs were provided by Michael Lee und imaged by Philip Pemberton
ROM_START( 3b1 )
	ROM_REGION16_BE(0x400000, "bootrom", 0)
	ROM_LOAD16_BYTE("72-00617.15c", 0x000000, 0x002000, CRC(4e93ff40) SHA1(1a97c8d32ec862f7f5fa1032f1688b76ea0672cc))
	ROM_LOAD16_BYTE("72-00616.14c", 0x000001, 0x002000, CRC(c61f7ae0) SHA1(ab3ac29935a2a587a083c4d175a5376badd39058))
ROM_END

} // anonymous namespace

/***************************************************************************
    GAME DRIVERS
***************************************************************************/

//    YEAR  NAME  PARENT  COMPAT  MACHINE  INPUT   CLASS         INIT        COMPANY  FULLNAME             FLAGS
COMP( 1985, 3b1,  0,      0,      unixpc,  unixpc, unixpc_state, empty_init, "AT&T",  "UNIX PC Model 3B1", MACHINE_NOT_WORKING )
