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
#include "screen.h"

#include "unixpc.lh"
#include "unixpc_dma.h"
#include "unixpc_fault.h"
#include "unixpc_hdc.h"
#include "unixpc_kbd.h"
#include "unixpc_mmu.h"
#include "unixpc_rtc.h"

#include <algorithm>
#include <cstdio>


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
	void mcr2_w(uint16_t data);

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
	uint32_t peek_phys32(uint32_t addr) const;
	uint32_t peek_logical32(uint32_t addr) const;
	void log_guest_cstr(const char *tag, uint32_t addr);
	void dump_vram_pbm(const char *path);

	TIMER_CALLBACK_MEMBER(dma_tick);
	TIMER_CALLBACK_MEMBER(kbd_clock);
	TIMER_CALLBACK_MEMBER(hdc_brdy_tick);

	void unixpc_mem(address_map &map) ATTR_COLD;

	bool dma_store_word(uint16_t word);
	uint16_t dma_fetch_word();
	void dma_service_floppy();
	void dma_service_hdc();
	void hdc_log_burst();
	void nmi_update();
	void kbd_scan();
	void kbd_shift_bit();

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
	bool m_mcr2_mirror = false;
	uint8_t m_hdc_head_low = 0;
	bool m_hdc_head4 = false;
	uint16_t m_hdc_cyl = 0;
	uint8_t m_hdc_sector = 0;
	uint8_t m_hdc_seccnt = 0;
	uint32_t m_hdc_next_lba = ~uint32_t(0);
	uint8_t m_hdc_lastcmd = 0;
	int m_hdc_trace_n = 0;
	int m_hdc_dma_idx = 0;
	int m_hdc_dma_words = 0;
	bool m_hdc_bdrq = false;
	uint32_t m_hdc_log_lba = ~uint32_t(0);
	unsigned m_hdc_log_head = 0;
	unsigned m_hdc_log_sec = 0;
	uint8_t m_fdc_hi = 0;
	bool m_fdc_have_hi = false;
	bool m_fdc_irq_latch = false;

	uint32_t m_key_down[4]{};
	bool m_keys_held = false;
	uint16_t m_kbd_shift = 0;
	int m_kbd_bits = 0;
	int m_kbd_cell = 0;
	bool m_kbd_rxd = true;
	bool m_kernel_bss_cleared = false;
	uint32_t m_fault_count = 0;
	uint32_t m_ifetch_hist[8]{};
	unsigned m_ifetch_hist_i = 0;
	bool m_logged_start_reenter = false;
	bool m_dumped_user_vram = false;
	bool m_protect_kernel_text = false;
	int m_autotype_step = 0;
	int m_autotype_hold = 0;
	int m_login_stable = 0;
	int m_autotype_pause = 0;
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
	uint16_t const old = m_mapram[offset];
	COMBINE_DATA(&m_mapram[offset]);
	// Kernel COFF text is identity in pages 0x02-0x1d. A user pfn of
	// 0x1A overwrites the BNE at 0x1AE58 (6616) with 0887, which jumps
	// to 0 and the start() trampoline. Keep those slots present-identity
	// after kernel entry; allow accessed/dirty/WE updates.
	if (m_protect_kernel_text && offset <= 0x1d)
	{
		uint16_t v = m_mapram[offset];
		uint16_t const ps = uint16_t((v >> 13) & 3);
		v = uint16_t((v & 0xfc00) | offset);
		if (ps == 0)
			v = uint16_t(v | 0x2000);
		m_mapram[offset] = v;
	}
	else if (m_protect_kernel_text && offset > 0x1d)
	{
		uint16_t const pfn = uint16_t(m_mapram[offset] & 0x3ff);
		if (pfn >= 0x02 && pfn <= 0x1d)
		{
			logerror("refuse kernel-text pfn %03x for slot %03x pc=%06x (was %04x)\n",
				unsigned(pfn), unsigned(offset),
				unsigned(m_maincpu->pc()), old);
			m_mapram[offset] = old;
		}
	}
	uint32_t const pc = unsigned(m_maincpu->pc());
	if ((offset <= 0x80 && m_fault_count != 0 && old != m_mapram[offset])
		|| (pc >= 0x8a90 && pc < 0x8ac6)
		|| offset == 0x70)
	{
		static int n;
		if (n < 80 || offset == 0x70)
		{
			logerror("map[%03x] %04x->%04x pc=%06x a7=%08x\n",
				unsigned(offset), old, m_mapram[offset], pc,
				unsigned(m_maincpu->state_int(M68K_A7)));
			if (n < 80)
				n++;
		}
	}
}

void unixpc_state::romlmap_w(int state)
{
	logerror("romlmap %d pc=%06x\n", state, unsigned(m_maincpu->pc()));
	m_ramromview.select(state ? 1 : 0);
	// Kernel reboot maps PROM over 0 and expects a CPU reset. Without
	// it, PC lands in the 0x800000 PROM window with the MMU still on.
	if (!state && m_protect_kernel_text)
	{
		logerror("reboot rom overlay\n");
		m_protect_kernel_text = false;
		m_maincpu->pulse_input_line(INPUT_LINE_RESET, attotime::zero);
	}
}

void unixpc_state::apply_cpu_fault(unixpc::access result, uint32_t logical, bool write)
{
	if (machine().side_effects_disabled())
		return;

	auto const enc = unixpc::encode_cpu_fault(unixpc::kind_from(result), write, logical, 16, m_board.pie());
	// Latch GSR/BSR on the first miss only (nested must not refresh).
	// Nested misses still BERR; returning 0xFFFF lets 0xc28 MOVEM walk A7.
	bool const latched = m_board.latch_cpu(enc);
	m_fault_count++;
	u16 const fc = m_maincpu->get_fc();
	bool const interesting = m_fault_count <= 48
		|| (m_fault_count % 4096) == 0
		|| (result != unixpc::access::page_fault && m_fault_count < 400);
	if (interesting)
	{
		logerror("cpu fault #%u res=%d addr=%06x wr=%d pc=%06x ppc=%06x fc=%u sr=%04x a0=%08x a1=%08x a7=%08x latch=%d gsr=%04x pte=%04x\n",
			m_fault_count, int(result), logical, int(write),
			unsigned(m_maincpu->pc()), unsigned(m_maincpu->pcbase()), unsigned(fc),
			unsigned(m_maincpu->state_int(M68K_SR)),
			unsigned(m_maincpu->state_int(M68K_A0)),
			unsigned(m_maincpu->state_int(M68K_A1)),
			unsigned(m_maincpu->state_int(M68K_A7)),
			int(latched), m_board.regs().genstat,
			m_mapper.entry(unixpc::mapper::page_index(logical)));
	}
	if (interesting && result == unixpc::access::kernel)
	{
		static int kw;
		if (kw++ < 3)
		{
			uint32_t const ppc = unsigned(m_maincpu->pcbase());
			uint32_t const a7 = uint32_t(m_maincpu->state_int(M68K_A7));
			uint32_t const a6 = uint32_t(m_maincpu->state_int(M68K_A6));
			logerror("  kernel-win d0=%08x d1=%08x a1=%08x a2=%08x a3=%08x a6=%08x ret=%08x a6ret=%08x a6arg=%08x words",
				uint32_t(m_maincpu->state_int(M68K_D0)),
				uint32_t(m_maincpu->state_int(M68K_D1)),
				uint32_t(m_maincpu->state_int(M68K_A1)),
				uint32_t(m_maincpu->state_int(M68K_A2)),
				uint32_t(m_maincpu->state_int(M68K_A3)),
				a6, peek_logical32(a7), peek_logical32(a6 + 4), peek_logical32(a6 + 8));
			for (int off = -24; off <= 8; off += 2)
				logerror(" %04x", uint16_t(peek_logical32(ppc + off) >> 16));
			logerror(" caller");
			uint32_t const ret = peek_logical32(a7);
			for (int off = -64; off <= 4; off += 2)
				logerror(" %04x", uint16_t(peek_logical32(ret + off) >> 16));
			logerror("\n");
			log_guest_cstr("a1str", uint32_t(m_maincpu->state_int(M68K_A1)));
			logerror("  data301000");
			for (uint32_t a = 0x301000; a < 0x301040; a += 4)
				logerror(" %08x", peek_logical32(a));
			logerror("\n  stackhi");
			for (uint32_t a = 0x2ffe80; a < 0x300000; a += 4)
				logerror(" %08x", peek_logical32(a));
			logerror("\n");
			log_guest_cstr("str1f00e", 0x1f00e);
			log_guest_cstr("str1f024", 0x1f024);
		}
	}
	if (m_board.ee())
		m_maincpu->set_buserror_details(logical, write ? 0 : 1, fc, true);
}

uint32_t unixpc_state::peek_phys32(uint32_t addr) const
{
	if (m_ramptr == nullptr || addr + 4 > m_ramsize)
		return 0;
	return (uint32_t(m_ramptr[addr >> 1]) << 16) | m_ramptr[(addr >> 1) + 1];
}

uint32_t unixpc_state::peek_logical32(uint32_t addr) const
{
	unixpc::probe const p0 = m_mapper.check(addr, false, true, false);
	unixpc::probe const p2 = m_mapper.check(addr + 2, false, true, false);
	if (p0.result != unixpc::access::ok || p2.result != unixpc::access::ok)
		return 0xffffffff;
	if (m_ramptr == nullptr || p0.physical + 2 > m_ramsize || p2.physical + 2 > m_ramsize)
		return 0xffffffff;
	return (uint32_t(m_ramptr[p0.physical >> 1]) << 16) | m_ramptr[p2.physical >> 1];
}

void unixpc_state::dump_vram_pbm(const char *path)
{
	FILE *const f = fopen(path, "w");
	if (f == nullptr)
		return;
	fprintf(f, "P4\n720 348\n");
	for (int y = 0; y < 348; y++)
		for (int x = 0; x < 720 / 16; x++)
		{
			uint16_t const w = m_videoram[y * (720 / 16) + x];
			uint8_t b0 = 0, b1 = 0;
			for (int b = 0; b < 8; b++)
				if (BIT(w, b))
					b0 |= uint8_t(0x80 >> b);
			for (int b = 8; b < 16; b++)
				if (BIT(w, b))
					b1 |= uint8_t(0x80 >> (b - 8));
			fputc(b0, f);
			fputc(b1, f);
		}
	fclose(f);
	logerror("dumped vram %s pc=%06x\n", path, unsigned(m_maincpu->pc()));
}

void unixpc_state::log_guest_cstr(const char *tag, uint32_t addr)
{
	char buf[96];
	unsigned n = 0;
	while (n + 1 < sizeof(buf) && addr + n < m_ramsize)
	{
		uint16_t const w = m_ramptr[(addr + n) >> 1];
		char const c = char((addr + n) & 1 ? (w & 0xff) : (w >> 8));
		if (c == 0)
			break;
		buf[n++] = (c >= 32 && c < 127) ? c : '.';
	}
	buf[n] = 0;
	logerror("%s @%06x \"%s\"\n", tag, addr, buf);
}

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
		if (p.result == unixpc::access::kernel && !super && fc == 1
			&& logical < 8)
			return 0;
		apply_cpu_fault(p.result, logical, false);
		return 0xffff;
	}
	if (p.physical >= m_ramsize)
		return 0xffff;

	if (!machine().side_effects_disabled() && (fc == 2 || fc == 6))
	{
		uint32_t const pc = logical;
		m_ifetch_hist[m_ifetch_hist_i++ & 7] = pc;
		if (m_fault_count >= 197 && pc >= 0x000cfe && pc < 0x000d80)
		{
			static int ck;
			if (ck++ < 8)
			{
				logerror("clockisr pc=%06x sr=%04x a7=%08x ops",
					pc, unsigned(m_maincpu->state_int(M68K_SR)),
					uint32_t(m_maincpu->state_int(M68K_A7)));
				if (m_ramptr != nullptr)
					for (uint32_t a = 0x0cfe; a < 0x0d40 && a + 2 <= m_ramsize; a += 2)
						logerror(" %04x", m_ramptr[a >> 1]);
				logerror(" 217c0=%08x 260b4=%08x 1f59a=%08x\n",
					peek_logical32(0x217c0), peek_logical32(0x260b4),
					peek_logical32(0x1f59a));
			}
		}
		// 0x1AE56 is JMP (A2) through the rastop table at $20BAC.
		// Slot 0 empty means wdinit never filled it; jumping to 0
		// hits the start() trampoline. Seed the 16 longs and retry
		// this jump — do not RTS-skip the blit (that leaves VRAM blank).
		if (pc == 0x001ae56 && uint32_t(m_maincpu->state_int(M68K_A2)) == 0
			&& m_protect_kernel_text && m_ramptr != nullptr
			&& (0x20bac + 4 * 16) <= m_ramsize)
		{
			// First impl after the 0x1C000 dispatcher; remaining
			// slots are the following boolean loops (see wind/wdinit).
			static uint32_t const impls[16] = {
				0x0001c076, 0x0001c076, 0x0001c076, 0x0001c076,
				0x0001cc96, 0x0001cc96, 0x0001cc96, 0x0001cf0e,
				0x0001d13e, 0x0001d332, 0x0001d51a, 0x0001d876,
				0x0001d9fa, 0x0001db9c, 0x0001dd24, 0x0001de72
			};
			for (int i = 0; i < 16; i++)
			{
				uint32_t const a = 0x20bac + uint32_t(i) * 4;
				m_ramptr[a >> 1] = uint16_t(impls[i] >> 16);
				m_ramptr[(a >> 1) + 1] = uint16_t(impls[i]);
			}
			m_maincpu->set_state_int(M68K_A2, impls[0]);
			m_maincpu->set_state_int(M68K_D1, impls[0]);
			logerror("JMP (A2=0) fill rastop table[0]=%06x\n", impls[0]);
		}
		if (m_fault_count >= 190 && pc < 0x0010)
		{
			static int z;
			if (z < 8)
			{
				logerror("ifetch0 pc=%06x sr=%04x a2=%08x a7=%08x op1ae58=%04x fromhist",
					pc, unsigned(m_maincpu->state_int(M68K_SR)),
					uint32_t(m_maincpu->state_int(M68K_A2)),
					uint32_t(m_maincpu->state_int(M68K_A7)),
					m_ramptr != nullptr ? m_ramptr[0x1ae58 >> 1] : 0);
				for (unsigned i = 0; i < 8; i++)
					logerror(" %06x", m_ifetch_hist[(m_ifetch_hist_i + i) & 7]);
				logerror(" vec78=%08x map00=%04x\n", peek_phys32(0x78), m_mapper.entry(0));
				z++;
			}
			if (pc == 0 && m_ramptr != nullptr && m_ramptr[0] == 0x4ef9)
				logerror("trampoline still JMP start op1ae58=%04x map1a=%04x\n",
					m_ramptr[0x1ae58 >> 1], m_mapper.entry(0x1a));
		}
		if (m_fault_count >= 190 && pc >= 0x1ae00 && pc < 0x1ae20)
		{
			static int e;
			if (e < 10)
			{
				uint32_t const a6 = uint32_t(m_maincpu->state_int(M68K_A6));
				logerror("strat pc=%06x d1=%08x a2=%08x a6=%08x 16(a6)=%08x 8(a6)=%08x\n",
					pc, uint32_t(m_maincpu->state_int(M68K_D1)),
					uint32_t(m_maincpu->state_int(M68K_A2)), a6,
					peek_logical32(a6 + 0x16), peek_logical32(a6 + 8));
				e++;
			}
		}
		if (m_fault_count >= 190 && pc >= 0x1ae40 && pc < 0x1ae80)
		{
			static int h;
			if (h < 12)
			{
				logerror("hdpath pc=%06x sr=%04x a0=%08x a1=%08x a2=%08x a7=%08x @25af4=%08x @25b14=%08x\n",
					pc, unsigned(m_maincpu->state_int(M68K_SR)),
					uint32_t(m_maincpu->state_int(M68K_A0)),
					uint32_t(m_maincpu->state_int(M68K_A1)),
					uint32_t(m_maincpu->state_int(M68K_A2)),
					uint32_t(m_maincpu->state_int(M68K_A7)),
					peek_logical32(0x25af4), peek_logical32(0x25b14));
				h++;
			}
		}
		if (m_fault_count >= 40 && !m_logged_start_reenter
			&& pc >= 0x0990 && pc < 0x09b4)
		{
			bool outside = true;
			for (unsigned i = 0; i < 8; i++)
			{
				uint32_t const p = m_ifetch_hist[i];
				if (p >= 0x0990 && p < 0x0c00 && p != pc)
					outside = false;
			}
			// First fetch in the spl7/identity-fill stub after user work.
			if (m_ifetch_hist_i > 4)
			{
				m_logged_start_reenter = true;
				uint32_t const a7 = uint32_t(m_maincpu->state_int(M68K_A7));
				uint32_t const a6 = uint32_t(m_maincpu->state_int(M68K_A6));
				logerror("reenter_start pc=%06x sr=%04x faults=%u a7=%08x a6=%08x a0=%08x a1=%08x a2=%08x a3=%08x d0=%08x d1=%08x\n",
					pc, unsigned(m_maincpu->state_int(M68K_SR)), m_fault_count,
					a7, a6,
					uint32_t(m_maincpu->state_int(M68K_A0)),
					uint32_t(m_maincpu->state_int(M68K_A1)),
					uint32_t(m_maincpu->state_int(M68K_A2)),
					uint32_t(m_maincpu->state_int(M68K_A3)),
					uint32_t(m_maincpu->state_int(M68K_D0)),
					uint32_t(m_maincpu->state_int(M68K_D1)));
				logerror("  tos=%08x arg=%08x a6old=%08x a6ret=%08x map00=%04x map02=%04x map70=%04x map80=%04x vec2=%08x vec78=%08x @25af4=%08x @25b14=%08x\n",
					peek_logical32(a7), peek_logical32(a7 + 4),
					peek_logical32(a6), peek_logical32(a6 + 4),
					m_mapper.entry(0x00), m_mapper.entry(0x02), m_mapper.entry(0x70), m_mapper.entry(0x80),
					peek_phys32(0x08), peek_phys32(0x78),
					peek_logical32(0x25af4), peek_logical32(0x25b14));
				logerror("  ifetch");
				for (unsigned i = 0; i < 8; i++)
					logerror(" %06x", m_ifetch_hist[(m_ifetch_hist_i + i) & 7]);
				logerror("\n");
				(void)outside;
				dump_vram_pbm("/tmp/3b1_prerestart.pbm");
			}
		}
	}

	return m_ramptr[p.physical >> 1];
}

void unixpc_state::ram_mmu_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	uint32_t const logical = uint32_t(offset) << 1;
	{
		static int n;
		if (n < 6)
		{
			logerror("mmu_w offset=%x logical=%06x data=%04x fc=%d\n", unsigned(offset), logical, data, int(m_maincpu->get_fc()));
			n++;
		}
	}
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
	// User must not store into the identity kernel-text physical pages.
	if (m_protect_kernel_text && !super
		&& p.physical >= 0x00002000 && p.physical < 0x0001e000)
	{
		apply_cpu_fault(unixpc::access::not_writable, logical, true);
		return;
	}
	if (logical >= 0x0001ae00 && logical < 0x0001af00)
		logerror("smash 1ae %06x <- %04x mask=%04x pc=%06x fc=%u a0=%08x a1=%08x a3=%08x\n",
			logical, data, mem_mask, unsigned(m_maincpu->pc()), unsigned(fc),
			uint32_t(m_maincpu->state_int(M68K_A0)),
			uint32_t(m_maincpu->state_int(M68K_A1)),
			uint32_t(m_maincpu->state_int(M68K_A3)));
	if (logical == 0x0001f59a || logical == 0x000217c0)
	{
		static int n;
		if (n++ < 24)
			logerror("tickvar %06x <- %04x pc=%06x phys=%06x\n",
				logical, data, unsigned(m_maincpu->pc()), p.physical);
	}
	if (p.physical < m_ramsize)
		COMBINE_DATA(&m_ramptr[p.physical >> 1]);
	if (logical == 0x5004 || logical == 0x5006 || logical == 0x4e2c)
		logerror("ram_mmu_w %06x <- %04x (res=%d phys=%06x)\n",
			logical, data, int(p.result), p.physical);
	// 0x2005c is in kernel .data. A self-store here is circular-queue
	// init; any other writer at that address during boot is corruption.
	if (logical == 0x2005c || logical == 0x2005e)
	{
		uint32_t const word = (m_ramptr != nullptr)
			? ((uint32_t(m_ramptr[0x2005c >> 1]) << 16) | m_ramptr[(0x2005c >> 1) + 1])
			: 0;
		uint32_t const a6s = uint32_t(m_maincpu->state_int(M68K_A6));
		uint32_t w14 = 0;
		if (m_ramptr != nullptr && a6s + 16 <= m_ramsize)
			w14 = (uint32_t(m_ramptr[(a6s + 14) >> 1]));
		logerror("store2005c pc=%06x data=%04x mask=%04x now=%08x phys=%06x a2=%08x a3=%08x d0=%08x a6=%08x w14=%04x\n",
			unsigned(m_maincpu->pc()), data, mem_mask, word, p.physical,
			uint32_t(m_maincpu->state_int(M68K_A2)),
			uint32_t(m_maincpu->state_int(M68K_A3)),
			uint32_t(m_maincpu->state_int(M68K_D0)),
			a6s, w14);
	}
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
	{
		static int n;
		if (n < 8)
		{
			logerror("phys_w %06x %04x\n", uint32_t(offset) << 1, data);
			n++;
		}
		COMBINE_DATA(&m_ramptr[offset]);
	}
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
	logerror("hdd exists=%d size=%u\n", int(m_hdr0->exists()), unsigned(m_ramsize));
}

void unixpc_state::machine_reset()
{
	m_board.reset();
	m_dma.reset();
	m_rtc.reset();
	m_kbd.reset();
	m_kbd_bits = 0;
	m_kbd_cell = 0;
	m_kbd_rxd = true;
	m_hdc_pos = 0;
	m_hdc_next_lba = ~uint32_t(0);
	m_hdc_trace_n = 0;
	m_hdc_dma_idx = 0;
	m_hdc_dma_words = 0;
	m_hdc_bdrq = false;
	m_hdc_log_lba = ~uint32_t(0);
	m_fdc_drq = false;
	m_kernel_bss_cleared = false;
	m_fault_count = 0;
	m_ifetch_hist_i = 0;
	m_logged_start_reenter = false;
	m_dumped_user_vram = false;
	m_protect_kernel_text = false;
	m_autotype_step = 0;
	m_autotype_hold = 0;
	m_login_stable = 0;
	m_autotype_pause = 0;
	if (m_hdr0->exists())
	{
		m_hdr0->set_geometry(unixpc::WINCH_CYLS, unixpc::WINCH_HEADS, unixpc::WINCH_SPT);
		logerror("hdd geom %u/%u/%u\n", unixpc::WINCH_CYLS, unixpc::WINCH_HEADS, unixpc::WINCH_SPT);
	}
	m_kbc->write_cts(0);
	m_kbc->write_dcd(0);
	m_maincpu->set_emmu_enable(true);
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
	logerror("bpplus %d pc=%06x\n", state, unsigned(m_maincpu->pc()));
	if (state && m_protect_kernel_text)
	{
		logerror("reboot bpplus\n");
		m_ramromview.select(0);
		m_protect_kernel_text = false;
		m_maincpu->pulse_input_line(INPUT_LINE_RESET, attotime::zero);
	}
}

void unixpc_state::vblank_w(int state)
{
	if (!state)
		return;
	uint32_t const pc = m_maincpu->pc();
	{
		static uint32_t last_pc;
		if ((last_pc & 0xff0000) == 0x800000 && (pc & 0xff0000) != 0x800000)
		{
			uint32_t const w0 = (m_ramptr != nullptr) ? ((uint32_t(m_ramptr[0x70000 >> 1]) << 16) | m_ramptr[(0x70000 >> 1) + 1]) : 0;
			uint32_t const w1 = (m_ramptr != nullptr) ? ((uint32_t(m_ramptr[0x70018 >> 1]) << 16) | m_ramptr[(0x70018 >> 1) + 1]) : 0;
			logerror("leave_rom from %06x to %06x sr=%04x a0=%08x a7=%08x ram70000=%08x ram70018=%08x\n",
				last_pc, pc,
				unsigned(m_maincpu->state_int(M68K_SR)),
				unsigned(m_maincpu->state_int(M68K_A0)),
				unsigned(m_maincpu->state_int(M68K_A7)),
				w0, w1);
		}
		if ((last_pc >= 0x070000 && last_pc < 0x080000)
			&& (pc == 0x0009d8 || pc == 0x0009a8))
		{
			uint32_t const at0 = (m_ramptr != nullptr) ? ((uint32_t(m_ramptr[0]) << 16) | m_ramptr[1]) : 0;
			uint32_t const at2k = (m_ramptr != nullptr) ? ((uint32_t(m_ramptr[0x2000 >> 1]) << 16) | m_ramptr[(0x2000 >> 1) + 1]) : 0;
			auto l = [&](uint32_t a) -> uint32_t {
				if (m_ramptr == nullptr || a + 4 > m_ramsize)
					return 0;
				return (uint32_t(m_ramptr[a >> 1]) << 16) | m_ramptr[(a >> 1) + 1];
			};
			m_protect_kernel_text = true;
			logerror("kernel_entry from %06x to %06x sr=%04x a7=%08x mem0=%08x mem2000=%08x rootdev=%08x bdevcnt=%08x bss20430=%08x at2005c=%08x at1e440=%08x at1e444=%08x at1e448=%08x\n",
				last_pc, pc, unsigned(m_maincpu->state_int(M68K_SR)),
				unsigned(m_maincpu->state_int(M68K_A7)), at0, at2k,
				l(0x1ed60), l(0x1ed58), l(0x20430), l(0x2005c), l(0x1e440), l(0x1e444), l(0x1e448));
			// Loader already zeros BSS before the COFF jump. A late fill here
			// races with main/binit/rqinit and can turn live queues into
			// self-pointers that hang the sleep/remrq NULL walk at 0x7514.
			if (m_ramptr != nullptr)
			{
				logerror("kernfn 74e0:");
				for (uint32_t a = 0x74e0; a < 0x7560 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 0a80:");
				for (uint32_t a = 0x0a80; a < 0x0b80 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 2fc0:");
				for (uint32_t a = 0x2fc0; a < 0x3080 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 06c0:");
				for (uint32_t a = 0x06c0; a < 0x0740 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 0c00:");
				for (uint32_t a = 0x0c00; a < 0x0d00 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 5fa4:");
				for (uint32_t a = 0x5fa4; a < 0x6020 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 63a0:");
				for (uint32_t a = 0x63a0; a < 0x6420 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 2020:");
				for (uint32_t a = 0x2020; a < 0x21c0 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 27e0:");
				for (uint32_t a = 0x27e0; a < 0x28c0 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 28c0:");
				for (uint32_t a = 0x28c0; a < 0x29a0 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn a6c0:");
				for (uint32_t a = 0xa6c0; a < 0xa720 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 8a80:");
				for (uint32_t a = 0x8a80; a < 0x8b00 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 0b40:");
				for (uint32_t a = 0x0b40; a < 0x0ba0 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 1ae00:");
				for (uint32_t a = 0x1ae00; a < 0x1ae70 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 5fa0:");
				for (uint32_t a = 0x5fa0; a < 0x6100 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\n gfx20bac:");
				for (uint32_t a = 0x20bac; a < 0x20bec && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 0cf0:");
				for (uint32_t a = 0x0cf0; a < 0x0d80 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror("\nkernfn 0de0:");
				for (uint32_t a = 0x0de0; a < 0x0e20 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
				logerror(" vec2=%08x vec78=%08x\n", l(0x08), l(0x78));
			}
		}
		if (pc >= 0x0704b0 && pc < 0x0704d0)
		{
			static int bssn;
			if (bssn < 8)
			{
				logerror("loader_bss pc=%06x a0=%08x a1=%08x d0=%08x d1=%08x\n",
					pc,
					unsigned(m_maincpu->state_int(M68K_A0)),
					unsigned(m_maincpu->state_int(M68K_A1)),
					unsigned(m_maincpu->state_int(M68K_D0)),
					unsigned(m_maincpu->state_int(M68K_D1)));
				bssn++;
			}
			// Loaded /unix (COFF tstart 0) has .data through $216F8 and
			// .bss $216F8-$2686C. start() at $9C8 sets those bounds;
			// the loader already zeros .bss. Do not wipe $20430: that
			// range is initialized data (rastop table, strings, queues).
		}
		last_pc = pc;
	}
	{
		static int n;
		bool const storm = m_fault_count > 200;
		static int stormn;
		if (((n++ % 60) == 0 && (pc < 0x800000)) || (storm && stormn++ < 15))
		{
			uint16_t op = 0;
			uint32_t vec6 = 0;
			if (m_ramptr != nullptr && (pc + 2) < m_ramsize)
				op = m_ramptr[pc >> 1];
			if (m_ramptr != nullptr && m_ramsize > 0x7c)
				vec6 = (uint32_t(m_ramptr[0x3c]) << 16) | m_ramptr[0x3d];
			uint16_t lbolt = 0;
			if (m_ramptr != nullptr && m_ramsize > 0x1f59c)
				lbolt = m_ramptr[0x1f59a >> 1];
			logerror("vbl pc=%06x sr=%04x op=%04x vec78=%08x faults=%u lbolt=%04x uop=%04x 260b8=%08x time=%08x\n",
				pc, unsigned(m_maincpu->state_int(M68K_SR)), op, vec6,
				m_fault_count, lbolt, uint16_t(peek_logical32(pc) >> 16),
				peek_logical32(0x260b8), peek_logical32(0x51d58));
			if ((pc & ~0xfff) == 0x00080000)
			{
				static int p80;
				if (p80++ < 2)
				{
					logerror("  text80");
					for (uint32_t a = 0x80000; a < 0x80020; a += 2)
						logerror(" %04x", uint16_t(peek_logical32(a) >> 16));
					logerror("\n");
				}
			}
			if ((pc & 0xffff00) == 0x31f800)
			{
				static int once;
				if (once++ < 3)
				{
					logerror("  ua31f");
					for (int off = -8; off <= 16; off += 2)
						logerror(" %04x", uint16_t(peek_logical32(pc + off) >> 16));
					logerror(" a0=%08x a1=%08x d0=%08x\n",
						uint32_t(m_maincpu->state_int(M68K_A0)),
						uint32_t(m_maincpu->state_int(M68K_A1)),
						uint32_t(m_maincpu->state_int(M68K_D0)));
					log_guest_cstr("a0str", uint32_t(m_maincpu->state_int(M68K_A0)));
					log_guest_cstr("a1str", uint32_t(m_maincpu->state_int(M68K_A1)));
				}
			}
			if (pc >= 0x8ac0 && pc < 0x8ae0)
				logerror("clrloop pc=%06x a3=%08x a0=%08x d0=%08x d1=%08x map3e=%04x map4e=%04x map70=%04x mem2000=%04x\n",
					pc,
					unsigned(m_maincpu->state_int(M68K_A3)),
					unsigned(m_maincpu->state_int(M68K_A0)),
					unsigned(m_maincpu->state_int(M68K_D0)),
					unsigned(m_maincpu->state_int(M68K_D1)),
					m_mapper.entry(0x3e), m_mapper.entry(0x4e), m_mapper.entry(0x70),
					m_ramptr[0x2000 >> 1]);
			if ((pc == 0x751a || pc == 0x7516 || pc == 0x7514) && m_ramptr != nullptr)
			{
				auto lg = [&](uint32_t a) -> uint32_t {
					if (a + 4 > m_ramsize)
						return 0;
					return (uint32_t(m_ramptr[a >> 1]) << 16) | m_ramptr[(a >> 1) + 1];
				};
				uint32_t const a6 = uint32_t(m_maincpu->state_int(M68K_A6));
				logerror("idle7516 a3=%08x a2=%08x a6=%08x ret=%08x local8=%08x w14=%04x head=%08x q %08x %08x %08x %08x\n",
					uint32_t(m_maincpu->state_int(M68K_A3)),
					uint32_t(m_maincpu->state_int(M68K_A2)),
					a6, lg(a6), lg(a6 + 8),
					(a6 + 16 <= m_ramsize) ? m_ramptr[(a6 + 14) >> 1] : 0,
					lg(0x1e9fa),
					lg(0x2005c), lg(0x20060), lg(0x8a5c), lg(0x0dcc));
			}
		}
	}
	if (!m_dumped_user_vram && (unsigned(m_maincpu->state_int(M68K_SR)) & 0x2000) == 0
		&& pc >= 0x080000 && m_fault_count >= 60)
	{
		m_dumped_user_vram = true;
		dump_vram_pbm("/tmp/3b1_user.pbm");
	}
	if (pc < 0x800000)
	{
		static int dumps;
		if ((dumps++ % 180) == 0)
		{
			FILE *const f = fopen("/tmp/3b1.pbm", "w");
			if (f != nullptr)
			{
				fprintf(f, "P4\n720 348\n");
				for (int y = 0; y < 348; y++)
					for (int x = 0; x < 720 / 16; x++)
					{
						uint16_t const w = m_videoram[y * (720 / 16) + x];
						uint8_t b0 = 0, b1 = 0;
						for (int b = 0; b < 8; b++)
							if (BIT(w, b))
								b0 |= uint8_t(0x80 >> b);
						for (int b = 8; b < 16; b++)
							if (BIT(w, b))
								b1 |= uint8_t(0x80 >> (b - 8));
						fputc(b0, f);
						fputc(b1, f);
					}
				fclose(f);
				logerror("dumped vram pc=%06x\n", pc);
			}
		}
	}
	m_board.vblank();
	bool irq6 = m_board.timer_latched();
	// While the PROM/loader still has identity vector $78, a 60 Hz IRQ
	// is fatal. Once the kernel is running (PC out of the boot ROM and
	// the $70000 loader), let the latch through.
	if (irq6 && pc >= 0x070000)
	{
		uint32_t vec = 0x78;
		if (m_ramptr != nullptr && m_ramsize > 0x80)
			vec = (uint32_t(m_ramptr[0x3c]) << 16) | m_ramptr[0x3d];
		if (vec < 0x100 || vec == 0x78)
			irq6 = false;
	}
	if (irq6 && m_fault_count >= 190)
	{
		static int iq;
		if (iq < 20)
		{
			uint32_t vec78 = 0;
			if (m_ramptr != nullptr && m_ramsize > 0x7c)
				vec78 = (uint32_t(m_ramptr[0x3c]) << 16) | m_ramptr[0x3d];
			logerror("irq6 pc=%06x sr=%04x vec78=%08x map00=%04x\n",
				pc, unsigned(m_maincpu->state_int(M68K_SR)), vec78, m_mapper.entry(0));
			iq++;
		}
	}
	m_maincpu->set_input_line(M68K_IRQ_6, irq6 ? ASSERT_LINE : CLEAR_LINE);
	if (pc < 0x070000 && !m_floppy->exists() && (m_fdc_intrq || m_fdc_irq_latch))
	{
		m_fdc_intrq = false;
		m_fdc_irq_latch = false;
		m_int02->in_w<1>(0);
	}
	if (pc >= 0x0086c0 && pc < 0x008740 && m_ramptr != nullptr
		&& (0x26024 + 2) <= m_ramsize)
	{
		uint16_t const shadow = m_ramptr[0x26024 >> 1];
		static int wl;
		if ((wl++ % 60) == 0)
		{
			uint32_t const a6 = uint32_t(m_maincpu->state_int(M68K_A6));
			logerror("waitloop 8(a6)=%08x 217c0=%08x 1e8de=%08x 1f59a=%08x 26024=%04x rast0=%08x\n",
				peek_logical32(a6 + 8), peek_logical32(0x217c0),
				peek_logical32(0x1e8de), peek_logical32(0x1f59a),
				shadow, peek_logical32(0x20bac));
		}
	}
	if (m_kbd_timer != nullptr && m_kbd_timer->enabled())
		kbd_scan();

	if ((pc < 0x070000 || pc >= 0x080000) && pc < 0x800000 && m_fault_count >= 300)
	{
		unsigned setwords = 0;
		for (int i = 0; i < (720 / 16) * 348; i++)
			setwords += unsigned(m_videoram[i] != 0);
		// Working is a full-screen stipple (most words set). Login is
		// a few hundred words of text on white. Do not type into working.
		// VBL samples often miss the wait-loop (clock ISR at $DEA), so
		// decay the counter instead of clearing it.
		bool const loginish = setwords >= 40 && setwords < 3000 && m_fault_count >= 300;
		if (loginish)
		{
			if (m_login_stable == 0)
				logerror("loginish vram_words=%u pc=%06x faults=%u\n",
					setwords, pc, m_fault_count);
			m_login_stable++;
		}
		else if (m_login_stable > 0)
			m_login_stable--;
		if (m_login_stable <= 0 && m_autotype_step == 0)
			return;
		m_autotype_hold++;
		if ((m_autotype_hold % (60 * 30)) == 0)
		{
			char path[72];
			std::snprintf(path, sizeof(path), "/tmp/3b1_t%u.pbm", unsigned(m_autotype_hold / 60));
			dump_vram_pbm(path);
		}
		if (m_autotype_step == 0 && m_login_stable == 60 * 10)
		{
			dump_vram_pbm("/tmp/3b1_pretype.pbm");
			uint32_t vec6c = 0;
			if (m_ramptr != nullptr && m_ramsize > 0x70)
				vec6c = (uint32_t(m_ramptr[0x36]) << 16) | m_ramptr[0x37];
			logerror("pretype vram_words=%u pc=%06x faults=%u vec6c=%08x\n",
				setwords, pc, m_fault_count, vec6c);
			m_autotype_step = 1;
		}
		// 0x00 in the stream is an 8-second pause (root shell, then login).
		if (m_autotype_step >= 1)
		{
			static uint8_t const seq[] = {
				0x72, 0x6f, 0x6f, 0x74, 0x0d, 0x0d,
				0x00,
				0x65, 0x78, 0x69, 0x74, 0x0d,
				0x00,
				0x69, 0x6e, 0x73, 0x74, 0x61, 0x6c, 0x6c, 0x0d, 0x0d
			};
			unsigned const n = unsigned(sizeof(seq));
			unsigned const i = unsigned(m_autotype_step - 1);
			if (i / 2 >= n)
				;
			else if (seq[i / 2] == 0x00)
			{
				if (m_autotype_pause == 0)
					logerror("autotype pause\n");
				m_autotype_pause++;
				if (m_autotype_pause >= 60 * 8)
				{
					m_autotype_pause = 0;
					m_autotype_step += 2;
					dump_vram_pbm("/tmp/3b1_phase.pbm");
					logerror("autotype resume vram_words=%u pc=%06x faults=%u\n",
						setwords, pc, m_fault_count);
				}
			}
			else if ((m_autotype_hold % 8) == 0)
			{
				if ((i % 2) == 0)
				{
					m_kbd.begin_keyboard_if_needed();
					m_kbd.push(uint8_t(seq[i / 2] | 0x80));
					logerror("autotype down %02x\n", seq[i / 2]);
				}
				else
				{
					m_kbd.push(unixpc::kbd_queue::ALL_KEYS_UP);
					logerror("autotype up\n");
				}
				m_autotype_step++;
			}
		}
	}
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
		logerror("dma store fault addr=%06x result=%d\n", logical, int(p.result));
		if (m_board.latch_dma(unixpc::encode_dma_fault(false, logical, m_board.pie())))
			nmi_update();
		return false;
	}
	if (m_protect_kernel_text
		&& p.physical >= 0x00002000 && p.physical < 0x0001e000)
	{
		logerror("dma store into kernel text phys=%06x logical=%06x\n", p.physical, logical);
		return false;
	}
	if (logical >= 0x0001ae00 && logical < 0x0001af00)
	{
		static int n;
		if (n < 8)
		{
			logerror("dma 1ae logical=%06x phys=%06x word=%04x pc=%06x\n",
				logical, p.physical, word, unsigned(m_maincpu->pc()));
			n++;
		}
	}
	if (p.physical < m_ramsize)
		m_ramptr[p.physical >> 1] = word;
	if (logical == 0x2005c || logical == 0x2005e)
		logerror("dma2005c word=%04x ptr=%06x phys=%06x pc=%06x\n",
			word, logical, p.physical, unsigned(m_maincpu->pc()));
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
		if (m_dma.reading())
			hdc_log_burst();
		// Last word of this 512: BRDY (count overflow). INTRQ is the
		// controller's job once sector_count hits 0.
		m_hdc->brdy_w(1);
	}
}

TIMER_CALLBACK_MEMBER(unixpc_state::dma_tick)
{
	dma_service_hdc();
	dma_service_floppy();
	uint32_t const pc = m_maincpu->pc();
	if (pc >= 0x70022 && pc < 0x800000)
	{
		static uint32_t last;
		static int n;
		if (pc != last && n < 80)
		{
			logerror("run pc=%06x sr=%04x\n", pc, unsigned(m_maincpu->state_int(M68K_SR)));
			last = pc;
			n++;
		}
	}
}

/***************************************************************************
    MISC
***************************************************************************/

uint16_t unixpc_state::gsr_r()
{
	uint16_t g = m_board.regs().genstat;
	if (m_board.pie())
		g |= 0x8400;
	{
		static int n;
		if (n < 12)
		{
			uint32_t const pcg = unsigned(m_maincpu->pc());
			uint32_t const a7 = uint32_t(m_maincpu->state_int(M68K_A7));
			uint16_t ssw = 0, fmt = 0;
			if (m_ramptr != nullptr && a7 + 0x44 < m_ramsize)
			{
				fmt = m_ramptr[(a7 + 0x06) >> 1];
				ssw = m_ramptr[(a7 + 0x08) >> 1];
			}
			logerror("gsr_r %04x bsr0=%04x bsr1=%04x pc=%06x a7=%08x fmt6=%04x ssw8=%04x off42=%04x",
				g, m_board.regs().bsr0, m_board.regs().bsr1, pcg, a7, fmt, ssw,
				(m_ramptr != nullptr && a7 + 0x44 < m_ramsize) ? m_ramptr[(a7 + 0x42) >> 1] : 0);
			if (m_ramptr != nullptr && n == 2)
			{
				logerror(" hdl");
				for (uint32_t a = 0x2000; a < 0x20c0 && a + 2 <= m_ramsize; a += 2)
					logerror(" %04x", m_ramptr[a >> 1]);
			}
			if (m_mapper.data() != nullptr)
				logerror(" map80=%04x map302=%04x",
					m_mapper.entry(0x80), m_mapper.entry(0x302));
			logerror("\n");
			n++;
		}
	}
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

void unixpc_state::mcr2_w(uint16_t data)
{
	// Word write: HDSEL3 is D0, P5.1 detect is D4. An 8-bit store to
	// the even address puts those bits in D8-D15 instead.
	uint8_t const v = uint8_t(data | (data >> 8));
	m_mcr2_mirror = BIT(v, 4);
	m_hdc_head4 = BIT(v, 0);
	m_hdc->head_w((m_hdc_head_low & 7) | (m_hdc_head4 ? 8 : 0));
	static int n;
	if (n++ < 24)
		logerror("mcr2 %04x hdsel3=%d pal=%d\n", data, int(m_hdc_head4), int(m_mcr2_mirror));
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
	// 838A / RS-232 baud: payload is on A1-A8, data bus ignored.
	(void)offset;
	(void)data;
	(void)mem_mask;
}

void unixpc_state::tcr_w(offs_t offset, uint16_t data)
{
	m_tcr->write_bit(offset >> 11, BIT(data, 14));
}

uint16_t unixpc_state::tsr_r()
{
	// b3..b0 active-low: msg waiting, ring2, ring1, offhook. Idle = all 1s.
	uint16_t data = 0x000f;
	if (m_mcr2_mirror)
		data |= 0x0010;
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

	//logerror("line_printer_r: %04x\n", data);

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
	{
		static int n;
		if (n < 16)
		{
			logerror("dma count=%04x en=%d rd=%d ptr=%06x src=%d\n",
				m_dma.count(), int(m_dma.enabled()), int(m_dma.reading()), m_dma.pointer(), int(m_dma.source()));
			n++;
		}
	}
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
	{
		static uint8_t last = 0xff;
		if (data != last)
		{
			logerror("disk_control %02x\n", data);
			last = data;
		}
	}
	m_hdc_head_low = uint8_t(data & 7);
	m_hdc->head_w(m_hdc_head_low);

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

	// Kernel iinit takes IRQ2 as floppy if LPSR bit 3 is still latched
	// from the PROM's empty-drive poll. Winchester select means FDC is idle.
	if (hd_sel && !m_floppy->exists())
	{
		m_fdc_intrq = false;
		m_fdc_irq_latch = false;
		m_int02->in_w<1>(0);
	}
}

void unixpc_state::wd2797_intrq_w(int state)
{
	if (state != m_fdc_intrq)
		logerror("fdc irq %d\n", state);
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
		logerror("fdc drq %d\n", state);
	m_fdc_drq = state != 0;
	if (m_fdc_drq)
		dma_service_floppy();
}

void unixpc_state::wd1010_intrq_w(int state)
{
	m_hdc_intrq = state;
	m_int02->in_w<0>(state);
}

void unixpc_state::hdc_reg_w(offs_t offset, uint8_t data)
{
	unsigned const reg = offset & 7;
	if (reg == 7)
	{
		m_hdc_lastcmd = data;
		m_hdc_next_lba = ~uint32_t(0);
		m_hdc->brdy_w(0);
		// A leftover empty-floppy INTRQ makes the kernel FD driver
		// print "#FD:Spurious interrupt" and panic iinit.
		if (!m_floppy->exists())
		{
			m_fdc_intrq = false;
			m_fdc_irq_latch = false;
			m_int02->in_w<1>(0);
		}
	}
	if (reg == 2)
		m_hdc_seccnt = data;
	if (reg == 3)
	{
		m_hdc_sector = data;
		m_hdc->head_w((m_hdc_head_low & 7) | (m_hdc_head4 ? 8 : 0));
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
		m_hdc->head_w(m_hdc_head_low | (m_hdc_head4 ? 8 : 0));
		// One Winchester; SDH drive-select bits must still find drive 0.
		data &= ~0x18;
	}
	if (reg == 7)
	{
		uint32_t const pc = m_maincpu->pc();
		if (pc < 0x080000)
		{
			unsigned const head = unsigned(m_hdc_head_low & 7) | (m_hdc_head4 ? 8u : 0u);
			uint8_t const sec = uint8_t(m_hdc_sector & 0x1f);
			logerror("hdc cmd %02x pc=%06x chs=%u:%u:%u nsec=%u bit5=%d mcr2=%d lba=%u ptr=%06x cnt=%04x\n",
				data, pc, unsigned(m_hdc_cyl), head, unsigned(sec),
				unsigned(m_hdc_seccnt), int((m_hdc_sector & 0x20) != 0),
				int(m_hdc_head4), unixpc::winchester_lba(m_hdc_cyl, head, sec),
				m_dma.pointer(), m_dma.count());
		}
	}
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
	uint32_t lba = (m_hdc_next_lba == ~uint32_t(0))
		? unixpc::winchester_lba(m_hdc_cyl, head, sec)
		: m_hdc_next_lba;

	{
		static int nb;
		if (nb++ < 40 || (nb % 200) == 0)
			logerror("hdc bdrq n=%d lba=%u chs=%u:%u:%u dma=%04x rd=%d\n",
				nb, lba, unsigned(m_hdc_cyl), unsigned(head), unsigned(sec),
				m_dma.count(), int(m_dma.reading()));
	}

	// One BDRQ is one 512-byte sector. Arm a 256-word DMA drain at
	// 1 MHz (20 words / 20 µs). Do not copy the sector here: an instant
	// fill plus a long BRDY delay lets the CPU start the next bio
	// (often a write) while a 1K read is still in flight. 1K (DMA
	// 0x3DFF) is two of these bursts; WD bit2 + sector_count=2 raises
	// BDRQ again. Do not drain the whole DMA on the first BDRQ.
	m_hdc_log_lba = lba;
	m_hdc_log_head = head;
	m_hdc_log_sec = unsigned(sec);
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
		hdc_log_burst();
	}
	m_hdc_next_lba = unixpc::winchester_lba_next(lba);
	if (lba != ~uint32_t(0) && lba < unixpc::WINCH_SECTORS)
		m_hdc->force_lbasector(int(lba));

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
	bool any = false;
	for (int bank = 0; bank < 4; bank++)
		now[bank] = m_keys[bank].read_safe(0);

	bool changed = false;
	for (int bank = 0; bank < 4; bank++)
		if (now[bank] != m_key_down[bank])
			changed = true;
	if (!changed)
		return;

	m_kbd.begin_keyboard_if_needed();

	bool held = false;
	uint8_t last = 0;
	bool have_last = false;
	for (int bank = 0; bank < 4; bank++)
	{
		held = held || now[bank] != 0;
		for (int bit = 0; bit < 32; bit++)
		{
			if (BIT(now[bank], bit))
			{
				if (have_last)
					m_kbd.push(last);
				last = uint8_t(bank * 32 + bit);
				have_last = true;
				any = true;
			}
		}
		m_key_down[bank] = now[bank];
	}

	if (have_last)
		m_kbd.push(uint8_t(last | 0x80));
	else if (!held && m_keys_held)
		m_kbd.push(unixpc::kbd_queue::ALL_KEYS_UP);
	m_keys_held = held;
	(void)any;
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
	for (int y = 0; y < 348; y++)
		for (int x = 0; x < 720/16; x++)
			for (int b = 0; b < 16; b++)
				bitmap.pix(y, x * 16 + b) = BIT(m_videoram[y * (720/16) + x], b);

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
	map(0xe20000, 0xe20001).w(FUNC(unixpc_state::mcr2_w));
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
	PORT_BIT(0x00002000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RETURN") PORT_CODE(KEYCODE_ENTER) PORT_CHAR(13)
	PORT_BIT(0x00000100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Backspace") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)
	PORT_BIT(0x00000200, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Tab") PORT_CODE(KEYCODE_TAB) PORT_CHAR(9)
	PORT_BIT(0x08000000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Esc") PORT_CODE(KEYCODE_ESC) PORT_CHAR(27)

	PORT_START("KEY1")
	PORT_BIT(0x00000001, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')
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
	PORT_BIT(0x00000100, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Shift") PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT)
	PORT_BIT(0x00001000, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Ctrl") PORT_CODE(KEYCODE_LCONTROL) PORT_CODE(KEYCODE_RCONTROL)
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

	PORT_START("MOUSEX")
	PORT_BIT(0xff, 0x00, IPT_MOUSE_X) PORT_SENSITIVITY(50) PORT_KEYDELTA(0)
	PORT_START("MOUSEY")
	PORT_BIT(0xff, 0x00, IPT_MOUSE_Y) PORT_SENSITIVITY(50) PORT_KEYDELTA(0)
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
		logerror("clrsint_high %d\n", state);
		m_board.set_clrsint_high(state != 0);
		if (!state)
			m_maincpu->set_input_line(M68K_IRQ_6, CLEAR_LINE);
	});

	// video hardware
	screen_device &screen(SCREEN(config, "screen"));
	screen.set_screen_update(FUNC(unixpc_state::screen_update));
	screen.set_raw(40_MHz_XTAL / 2, 896, 0, 720, 367, 0, 348);
	screen.set_palette("palette");
	screen.screen_vblank().set(FUNC(unixpc_state::vblank_w));
	// vsync should actually last 17264 pixels

	config.set_default_layout(layout_unixpc);

	PALETTE(config, "palette", palette_device::MONOCHROME);

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
	m_kbc->irq_handler().set([this](int state)
	{
		uint32_t vec = 0;
		if (m_ramptr != nullptr && m_ramsize > 0x70)
			vec = (uint32_t(m_ramptr[0x36]) << 16) | m_ramptr[0x37];
		if (vec > 0x100 && vec != 0x6c)
			m_maincpu->set_input_line(M68K_IRQ_3, state);
	});

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
COMP( 1985, 3b1,  0,      0,      unixpc,  unixpc, unixpc_state, empty_init, "AT&T",  "UNIX PC Model 3B1", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
