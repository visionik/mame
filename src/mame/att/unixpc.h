// license:GPL-2.0+
// copyright-holders:Dirk Best, R. Belmont, Visionik
/***************************************************************************

    AT&T UNIX PC series (7300 and 3B1)

    Original skeleton: Dirk Best and R. Belmont

    The 3B1 is a 10 MHz 68010 with a custom 4K-page mapper (not a 68851),
    WD2797 floppy, WD2010-class MFM hard disk, and a 720x348 mono bitmap.
    UNIX 3.51 demand-pages through map RAM. Faults report via GENSTAT and
    a 68010 bus error (CPU) or level-7 NMI (DMA).

    Disk DMA is a board engine: 24-bit pointer, 14-bit word count, DRQ
    from the selected controller. The 60 Hz interrupt is a latch cleared
    by MCR.CLRSINT- (active low), not a timer enable bit.

***************************************************************************/

#ifndef MAME_ATT_UNIXPC_H
#define MAME_ATT_UNIXPC_H

#pragma once

#include "emu.h"

#include "cpu/m68000/m68010.h"
#include "imagedev/floppy.h"
#include "imagedev/harddriv.h"
#include "machine/6850acia.h"
#include "machine/74259.h"
#include "machine/output_latch.h"
#include "machine/clock.h"
#include "machine/input_merger.h"
#include "machine/ram.h"
#include "machine/tc8250.h"
#include "machine/wd1010.h"
#include "machine/wd_fdc.h"
#include "machine/z80sio.h"

#include "bus/centronics/ctronics.h"
#include "bus/rs232/rs232.h"

#include "emupal.h"
#include "screen.h"
#include "sound/beep.h"

#include "unixpc_exp.h"
#include "unixpc_dma.h"
#include "unixpc_fault.h"
#include "unixpc_hdc.h"
#include "unixpc_io.h"
#include "unixpc_kbd.h"
#include "unixpc_mmu.h"
#include "unixpc_rtc.h"
#include "unixpc_serial.h"
#include "unixpc_slot.h"
#include "unixpc_sound.h"
#include "unixpc_video.h"

#include <unordered_set>

#ifndef UNIXPC_HDC_TRACE
#define UNIXPC_HDC_TRACE 0
#endif
#ifndef UNIXPC_USER_TRACE
#define UNIXPC_USER_TRACE 0
#endif
#ifndef UNIXPC_BERR_TRACE
#define UNIXPC_BERR_TRACE 0
#endif
#ifndef UNIXPC_FDC_TRACE
#define UNIXPC_FDC_TRACE 1
#endif

class unixpc_state : public driver_device
{
public:
	unixpc_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_gcr(*this, "gcr"),
		m_tcr(*this, "tcr"),
		m_int02(*this, "int02"),
		m_centronics(*this, "printer"),
		m_printlatch(*this, "printlatch"),
		m_int1(*this, "int1"),
		m_int5(*this, "int5"),
		m_exp(*this, "exp%u", 0U),
		m_ram(*this, RAM_TAG),
		m_wd2797(*this, "wd2797"),
		m_floppy(*this, "wd2797:0:525dd"),
		m_hdc(*this, "hdc"),
		m_hdr0(*this, "hdc:0"),
		m_hdr1(*this, "hdc:1"),
		m_kbc(*this, "kbc"),
		m_rtc(*this, "rtc"),
		m_mpsc(*this, "mpsc"),
		m_rs232(*this, "rs232"),
		m_rs232_brg(*this, "rs232_brg"),
		m_modem_brg(*this, "modem_brg"),
		m_keys(*this, "KEY%u", 0U),
		m_mouse_x(*this, "MOUSEX"),
		m_mouse_y(*this, "MOUSEY"),
		m_mouse_b(*this, "MOUSEB"),
		m_phosphor(*this, "PHOSPHOR"),
		m_mouse_acc(*this, "MOUSEACC"),
		m_winscale(*this, "WINSCALE"),
		m_diskcfg(*this, "DISKCFG"),
		m_paritycfg(*this, "PARITY"),
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
	void state_presave();
	void state_postload();

	uint16_t line_printer_r();
	void printer_busy_w(int state);
	void printer_select_w(int state);
	void printer_paper_w(int state);
	void printer_fault_w(int state);
	void printer_ack_w(int state);
	void printer_strobe_w(int state);
	void printer_data_w(offs_t offset, uint16_t data, uint16_t mem_mask);
	void printer_int_clear();
	void lp_apply_strobe();
	void lp_trace(char const *what, unsigned extra);
	uint16_t exp_io_r(offs_t offset);
	void exp_io_w(offs_t offset, uint16_t data, uint16_t mem_mask);
	unixpc_card_device *exp_card(unsigned window) const;
	friend class unixpc_enet_device;
	void disk_control_w(uint8_t data);
	void gcr_w(offs_t offset, uint16_t data);
	void romlmap_w(int state);
	void error_enable_w(int state);
	void parity_enable_w(int state);
	void bpplus_w(int state);
	void vblank_w(int state);
	uint16_t ram_mmu_r(offs_t offset, uint16_t mem_mask = ~0);
	void ram_mmu_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void ram_phys_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	bool ram_word(uint32_t phys, bool write, uint32_t &word) const;
	uint16_t mapram_r(offs_t offset);
	void mapram_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	uint16_t bootrom_mirror_r(offs_t offset);
	void apply_cpu_fault(unixpc::access result, uint32_t logical, bool write);
	void enforce_user_highmem(offs_t addr, uint16_t mem_mask, bool write);
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
	uint8_t mpsc_cd_r(offs_t offset);
	void mpsc_cd_w(offs_t offset, uint8_t data);
	void rs232_clock_w(int state);
	void modem_clock_w(int state);
	void modem_brg_select_w(int state);
	void mpsc_irq_w(int state);
	void update_mpsc_irq();

	TIMER_CALLBACK_MEMBER(dma_tick);
	TIMER_CALLBACK_MEMBER(kbd_clock);
	TIMER_CALLBACK_MEMBER(hdc_brdy_tick);
	TIMER_CALLBACK_MEMBER(lp_ack_tick);

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
	unsigned hdc_head() const;
	harddisk_image_device *hdc_image() const;
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
	required_device<centronics_device> m_centronics;
	required_device<output_latch_device> m_printlatch;
	required_device<input_merger_device> m_int1;
	required_device<input_merger_device> m_int5;
	required_device_array<unixpc_exp_device, 3> m_exp;
	required_device<ram_device> m_ram;
	required_device<wd2797_device> m_wd2797;
	required_device<floppy_image_device> m_floppy;
	required_device<wd1010_device> m_hdc;
	required_device<harddisk_image_device> m_hdr0;
	required_device<harddisk_image_device> m_hdr1;
	required_device<acia6850_device> m_kbc;
	required_device<tc8250_device> m_rtc;
	required_device<upd7201_device> m_mpsc;
	required_device<rs232_port_device> m_rs232;
	required_device<clock_device> m_rs232_brg;
	required_device<clock_device> m_modem_brg;
	optional_ioport_array<unixpc::KEY_BANKS> m_keys;
	required_ioport m_mouse_x;
	required_ioport m_mouse_y;
	required_ioport m_mouse_b;
	required_ioport m_phosphor;
	required_ioport m_mouse_acc;
	required_ioport m_winscale;
	required_ioport m_diskcfg;
	required_ioport m_paritycfg;
	unixpc::disk_combo m_combo = unixpc::DISK;
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
	unixpc::kbd_queue m_kbd;

	// Forced bad-parity (BPPLUS) for the diagnostic Memory & Parity
	// subtest. Cached from Machine Configuration at reset; poison is a
	// sparse set of physical RAM word indices. Lookups run only when
	// the option is on, PIE is set, and the set is non-empty.
	bool m_parity_emul = false;
	bool m_bpplus = false;
	std::unordered_set<uint32_t> m_poison;
	// Sorted snapshot of m_poison for save states (unordered_set is not saveable).
	std::vector<uint32_t> m_poison_save;

	emu_timer *m_dma_timer = nullptr;
	emu_timer *m_kbd_timer = nullptr;
	emu_timer *m_hdc_brdy_timer = nullptr;
	emu_timer *m_lp_ack_timer = nullptr;
	memory_passthrough_handler m_user_io_tap;

	uint8_t m_hdc_buf[unixpc::SECTOR_BYTES]{};
	u16 m_hdc_pos = 0;
	bool m_fdc_drq = false;

	uint8_t m_hdc_head_low = 0;
	bool m_hdc_head4 = false;
	bool m_hdc_drive1 = false;
	bool m_hdc_xfer1 = false;
	bool m_mcr2_p51 = false;
	uint16_t m_hdc_cyl = 0;
	uint8_t m_hdc_sector = 0;
	uint8_t m_hdc_seccnt = 0;
	uint32_t m_hdc_next_lba = ~uint32_t(0);
#if UNIXPC_HDC_TRACE
	s32 m_hdc_trace_n = 0;
	uint32_t m_hdc_log_lba = ~uint32_t(0);
	unsigned m_hdc_log_head = 0;
	unsigned m_hdc_log_sec = 0;
#endif
	s32 m_hdc_dma_idx = 0;
	s32 m_hdc_dma_words = 0;
	bool m_hdc_bdrq = false;
	uint8_t m_fdc_hi = 0;
	bool m_fdc_have_hi = false;
	// Write path: low byte of the fetched DMA word still owed to the next DRQ.
	uint8_t m_fdc_pending_lo = 0;
	bool m_fdc_have_lo = false;
#if UNIXPC_FDC_TRACE
	s32 m_fdc_trace_n = 0;
	s32 m_fdc_sec_bytes = 0;
	uint8_t m_fdc_sec_head[8]{};
	uint32_t m_fdc_sec_ptr = 0;
	// Recent sector DMA completions for paired re-read dumps.
	static constexpr int FDC_HIST = 32;
	uint8_t m_fdc_hist_trk[FDC_HIST]{};
	uint8_t m_fdc_hist_sec[FDC_HIST]{};
	uint8_t m_fdc_hist_side[FDC_HIST]{};
	uint32_t m_fdc_hist_ptr[FDC_HIST]{};
	s32 m_fdc_hist_n[FDC_HIST]{};
	int m_fdc_hist_count = 0;
#endif
	bool m_fdc_irq_latch = false;
	bool m_lp_busy = true;
	bool m_lp_select = true;
	bool m_lp_paper = true;
	bool m_lp_fault = true;
	bool m_lp_ack = true;
	bool m_lp_int = false;
	bool m_lp_hold_busy = false;
	bool m_lp_need_strobe_release = false;
	int m_lp_strobe = 1;

	uint32_t m_key_down[unixpc::KEY_BANKS]{};
	uint32_t m_key_seen[unixpc::KEY_BANKS]{};
	s32 m_key_stable = 0;
	bool m_keys_held = false;
	s32 m_mouse_lx = 0;
	s32 m_mouse_ly = 0;
	uint8_t m_mouse_lb = 0;
	uint8_t m_mouse_sent_b = 0;
	s32 m_mouse_pdx = 0;
	s32 m_mouse_pdy = 0;
	bool m_mouse_inited = false;

	s32 m_win_view = -1;
	uint16_t m_dialer_word = 0;
	bool m_mpsc_irq = false;
	uint16_t m_kbd_shift = 0;
	u8 m_kbd_bits = 0;
	u8 m_kbd_cell = 0;
	bool m_kbd_rxd = true;
};


#endif
