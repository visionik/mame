// license:GPL-2.0+
// copyright-holders:Dirk Best, R. Belmont, Visionik

#include "unixpc.h"

#include "emuopts.h"
#include "bus/centronics/ctronics.h"
#include "bus/rs232/rs232.h"
#include "dipty.h"
#include "machine/output_latch.h"
#include "render.h"
#include "speaker.h"

#include <algorithm>

#include "unixpc.lh"

#define VERBOSE (0)
#include "logmacro.h"

ALLOW_SAVE_TYPE(unixpc::board_rev)
ALLOW_SAVE_TYPE(unixpc::hdc_chip)

// A second harddisk_image_device would number both as -hard1 and -hard2.
// This one reports its own type so the boot image stays on -hard.
class unixpc_hard2_image_device : public harddisk_image_device
{
public:
	unixpc_hard2_image_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual const char *image_type_name() const noexcept override { return "harddisk2"; }
	virtual const char *image_brief_type_name() const noexcept override { return "hard2"; }
};

DEFINE_DEVICE_TYPE(UNIXPC_HARD2, unixpc_hard2_image_device, "unixpc_hard2", "UNIX PC Second Hard Disk")

unixpc_hard2_image_device::unixpc_hard2_image_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: harddisk_image_device(mconfig, UNIXPC_HARD2, tag, owner, clock)
{
}


// Machine configuration, address map, and ROM.


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
	LOG("UI_TOGGLE Cmd+Shift+U, UI_MENU Tab, UI_RELEASE_POINTER Cmd+Shift+M\n");
}


void unixpc_state::machine_start()
{
	m_ramptr = m_ram->pointer<uint16_t>();
	m_ramsize = m_ram->size();
	m_mapper.bind(&m_mapram[0]);
	m_board.reset();
	m_dma.reset();
	m_kbd.reset();

	m_dma_timer = timer_alloc(FUNC(unixpc_state::dma_tick), this);
	m_dma_timer->adjust(attotime::from_usec(unixpc::DMA_SLICE_USEC), 0,
		attotime::from_usec(unixpc::DMA_SLICE_USEC));

	// 16x 1200 baud keyboard clock into the 6850.
	m_kbd_timer = timer_alloc(FUNC(unixpc_state::kbd_clock), this);
	m_kbd_timer->adjust(attotime::from_hz(unixpc::KBD_BAUD * unixpc::KBD_CLOCK_MUL), 0, attotime::from_hz(unixpc::KBD_BAUD * unixpc::KBD_CLOCK_MUL));
	m_hdc_brdy_timer = timer_alloc(FUNC(unixpc_state::hdc_brdy_tick), this);
	m_lp_ack_timer = timer_alloc(FUNC(unixpc_state::lp_ack_tick), this);

	save_item(NAME(m_fdc_intrq));
	save_item(NAME(m_hdc_intrq));
	save_item(NAME(m_mpsc_irq));
	save_item(NAME(m_fdc_drq));
	save_item(NAME(m_hdc_buf));
	save_item(NAME(m_hdc_pos));
	save_item(NAME(m_hdc_head_low));
	save_item(NAME(m_hdc_head4));
	save_item(NAME(m_hdc_drive1));
	save_item(NAME(m_hdc_xfer1));
	save_item(NAME(m_mcr2_p51));
	save_item(NAME(m_hdc_cyl));
	save_item(NAME(m_hdc_sector));
	save_item(NAME(m_hdc_seccnt));
	save_item(NAME(m_hdc_next_lba));
	save_item(NAME(m_hdc_dma_idx));
	save_item(NAME(m_hdc_dma_words));
	save_item(NAME(m_hdc_bdrq));
	save_item(NAME(m_fdc_hi));
	save_item(NAME(m_fdc_have_hi));
	save_item(NAME(m_fdc_pending_lo));
	save_item(NAME(m_fdc_have_lo));
	save_item(NAME(m_fdc_irq_latch));
	save_item(NAME(m_lp_busy));
	save_item(NAME(m_lp_select));
	save_item(NAME(m_lp_paper));
	save_item(NAME(m_lp_fault));
	save_item(NAME(m_lp_ack));
	save_item(NAME(m_lp_int));
	save_item(NAME(m_lp_hold_busy));
	save_item(NAME(m_lp_need_strobe_release));
	save_item(NAME(m_lp_strobe));
	save_item(NAME(m_key_down));
	save_item(NAME(m_key_seen));
	save_item(NAME(m_key_stable));
	save_item(NAME(m_keys_held));
	save_item(NAME(m_mouse_lx));
	save_item(NAME(m_mouse_ly));
	save_item(NAME(m_mouse_lb));
	save_item(NAME(m_mouse_sent_b));
	save_item(NAME(m_mouse_pdx));
	save_item(NAME(m_mouse_pdy));
	save_item(NAME(m_mouse_inited));
	save_item(NAME(m_win_view));
	save_item(NAME(m_dialer_word));
	save_item(NAME(m_kbd_shift));
	save_item(NAME(m_kbd_bits));
	save_item(NAME(m_kbd_cell));
	save_item(NAME(m_kbd_rxd));
	save_item(NAME(m_parity_emul));
	save_item(NAME(m_bpplus));
	save_item(NAME(m_combo.board));
	save_item(NAME(m_combo.chip));
	save_item(NAME(m_poison_save));
	unixpc::unixpc_save_dma(*this, m_dma);
	unixpc::unixpc_save_board(*this, m_board);
	unixpc::unixpc_save_kbd(*this, m_kbd);
	unixpc::unixpc_save_mapper(*this, m_mapper);
	machine().save().register_presave(save_prepost_delegate(FUNC(unixpc_state::state_presave), this));
	machine().save().register_postload(save_prepost_delegate(FUNC(unixpc_state::state_postload), this));
	LOG("hdd exists=%d size=%u\n", int(m_hdr0->exists()), unsigned(m_ramsize));

	// I/O / ROM / expansion sit above 4 MiB and are not walked through
	// ram_mmu_*. Trap user FC here so Diagnostic Disk USER I/O sees a BERR
	// (FreeBee checkMemoryAccess MEM_UIE). VIDPAL user VRAM is exempt.
	m_user_io_tap = m_maincpu->space(AS_PROGRAM).install_readwrite_tap(
		unixpc::MAPRAM_BASE, 0xffffff, "user_highmem",
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			(void)data;
			enforce_user_highmem(offset, mem_mask, false);
		},
		[this] (offs_t offset, u16 &data, u16 mem_mask)
		{
			(void)data;
			enforce_user_highmem(offset, mem_mask, true);
		});

	unixpc_bind_ui_menu();
}


void unixpc_state::state_presave()
{
	m_poison_save.assign(m_poison.begin(), m_poison.end());
	std::sort(m_poison_save.begin(), m_poison_save.end());
}


void unixpc_state::state_postload()
{
	m_ramptr = m_ram->pointer<uint16_t>();
	m_ramsize = m_ram->size();
	m_mapper.bind(&m_mapram[0]);
	m_poison.clear();
	m_poison.insert(m_poison_save.begin(), m_poison_save.end());
	// Re-drive LPINT into the level-2 merger from the restored flip-flop.
	m_int02->in_w<2>(m_lp_int ? 1 : 0);
	lp_apply_strobe();
	// Keyboard bit-bang mid-frame after a load looks like a break/framing
	// error to the 6850 and drops keys until the line idles again.
	m_kbd_bits = 0;
	m_kbd_cell = 0;
	m_kbd_rxd = true;
	m_kbc->write_rxd(1);
	update_mpsc_irq();
	m_maincpu->set_input_line(M68K_IRQ_6, m_board.timer_latched() ? ASSERT_LINE : CLEAR_LINE);
}


void unixpc_state::machine_reset()
{
	m_board.reset();
	m_dma.reset();
	m_kbd.reset();
	// -parity / -noparity (or ini) override Machine Configuration when set.
	{
		auto const entry = machine().options().get_entry(OPTION_UNIXPC_PARITY);
		if (entry != nullptr && entry->priority() > OPTION_PRIORITY_DEFAULT)
			m_parity_emul = entry->bool_value();
		else
			m_parity_emul = BIT(m_paritycfg->read(), 0);
	}
	m_bpplus = false;
	m_poison.clear();
	m_poison_save.clear();
	osd_printf_info("unixpc: RAM parity emulation %s\n", m_parity_emul ? "ON" : "OFF");
	// UNIX 3.51 maps years 00-86 into 2000-2086. Keep the live clock but
	// pin the year at 87 so the kernel stays in the 20th century.
	{
		system_time now;
		machine().current_datetime(now);
		auto const &t = now.local_time;
		m_rtc->set_time(true, unixpc::RTC_UNIX351_YEAR, t.month + 1, t.mday,
			t.weekday + 1, t.hour, t.minute, t.second);
	}
	m_key_stable = 0;
	m_keys_held = false;
	m_mouse_inited = false;
	m_mouse_pdx = 0;
	m_mouse_pdy = 0;
	m_mouse_sent_b = 0;
	m_dialer_word = 0;
	m_mpsc_irq = false;
	// 9600 until the dialer page says otherwise. A stopped clock leaves
	// the queued bytes in the tty and a reader on tty000 waits forever.
	m_rs232_brg->set_clock(unixpc::rs232_clock_hz(2));
	m_modem_brg->set_clock(0);
	// The 7201 CTS and DCD pins are active-low. An empty slot leaves
	// them deasserted, so the transmitter never starts. Hold both ready
	// unless a plug (loopback, pty, null_modem, …) is driving them.
	if (m_rs232->get_card_device() == nullptr)
	{
		m_mpsc->ctsa_w(0);
		m_mpsc->dcda_w(0);
	}
	m_maincpu->set_input_line(M68K_IRQ_4, CLEAR_LINE);
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
	m_lp_hold_busy = false;
	m_lp_need_strobe_release = false;
	m_lp_strobe = 1;
	if (m_lp_ack_timer)
		m_lp_ack_timer->adjust(attotime::never);
	printer_int_clear();
	lp_apply_strobe();
#if UNIXPC_HDC_TRACE
	m_hdc_log_lba = ~uint32_t(0);
#endif
	m_fdc_drq = false;
	m_fdc_have_hi = false;
	m_fdc_have_lo = false;
	m_mcr2_p51 = false;
	m_hdc_head4 = false;
	m_hdc_drive1 = false;
	m_hdc_xfer1 = false;
	// Ports are not readable during machine_start.
	uint8_t const diskcfg = m_diskcfg->read();
	m_combo.board = (diskcfg & 0x01) ? unixpc::board_rev::p51 : unixpc::board_rev::p3_p5;
	m_combo.chip = (diskcfg & 0x02) ? unixpc::hdc_chip::wd2010 : unixpc::hdc_chip::wd1010;
	if (m_hdr0->exists())
	{
		m_hdr0->set_geometry(m_combo.cylinders(), m_combo.heads(), unixpc::WINCH_SPT);
		LOG("hdd geom %u/%u/%u\n", unsigned(m_combo.cylinders()), m_combo.heads(), unixpc::WINCH_SPT);
	}
	if (m_hdr1->exists())
		m_hdr1->set_geometry(m_combo.cylinders(), m_combo.heads(), unixpc::WINCH_SPT);
	m_kbc->write_cts(0);
	m_kbc->write_dcd(0);
	m_maincpu->set_emmu_enable(true);
	unixpc_bind_ui_menu();
	disk_control_w(0);
	m_board.set_clrsint_high(false);
	m_maincpu->set_input_line(M68K_IRQ_6, CLEAR_LINE);
	m_ramromview.select(0);
}


void unixpc_state::vblank_w(int state)
{
	if (!state)
		return;

	// A 7201 interrupt that arrived while the PROM was running is still
	// pending; deliver it once execution has left the boot ROM.
	update_mpsc_irq();

	{
		static bool once;
		if (!once)
		{
			once = true;
			std::string slave;
			for (device_pty_interface &pty : pty_interface_enumerator(machine().root_device()))
			{
				if (pty.is_open() && !pty.slave_name().empty())
				{
					slave = pty.slave_name();
					break;
				}
			}
			if (!slave.empty())
				osd_printf_info("RS-232 pty %s\n", slave);
			machine().popmessage("screen %s   |   UI: Cmd+Shift+U then Tab",
				slave.empty() ? "(no pty)" : slave.c_str());
		}
	}

	// IPT_UI_RELEASE_POINTER: OSD toggles sticky host-pointer free without
	// pausing (sdl_osd_interface / mac_osd_interface::check_osd_inputs).

	int const view = int(m_winscale->read() & unixpc::WIN_VIEW_MASK);
	if (view != m_win_view)
	{
		m_win_view = view;
		render_target *const target = machine().render().first_target();
		if (target != nullptr)
			target->set_view(target->configured_view(unixpc::WIN_VIEW_NAME[view], 0, 1));
	}

	m_board.vblank();
	bool const irq6 = m_board.timer_latched();
	m_maincpu->set_input_line(M68K_IRQ_6, irq6 ? ASSERT_LINE : CLEAR_LINE);

	if (m_kbd_timer != nullptr && m_kbd_timer->enabled())
		kbd_scan();
}



/***************************************************************************
    ADDRESS MAPS
***************************************************************************/

void unixpc_state::unixpc_mem(address_map &map)
{
	map(unixpc::RAM_WINDOW, unixpc::RAM_END).view(m_ramromview);
	m_ramromview[0](unixpc::RAM_WINDOW, unixpc::RAM_END).rw(FUNC(unixpc_state::bootrom_mirror_r), FUNC(unixpc_state::ram_phys_w));
	m_ramromview[1](unixpc::RAM_WINDOW, unixpc::RAM_END).rw(FUNC(unixpc_state::ram_mmu_r), FUNC(unixpc_state::ram_mmu_w));
	// Zone A is decoded on A19-A16 only; the rest of each 64K block mirrors.
	map(unixpc::MAPRAM_BASE, unixpc::MAPRAM_BASE + unixpc::MAPRAM_SIZE - 1).mirror(unixpc::MAPRAM_MIRROR).rw(FUNC(unixpc_state::mapram_r), FUNC(unixpc_state::mapram_w)).share("mapram");
	map(unixpc::GSR_BASE, unixpc::GSR_BASE + 1).mirror(unixpc::ZONE_WORD_MIRROR).r(FUNC(unixpc_state::gsr_r));
	map(unixpc::VRAM_BASE, unixpc::VRAM_BASE + unixpc::VRAM_SIZE - 1).ram().share("videoram");
	map(unixpc::BSR0_BASE, unixpc::BSR0_BASE + 1).mirror(unixpc::ZONE_WORD_MIRROR).r(FUNC(unixpc_state::bsr0_r));
	map(unixpc::BSR1_BASE, unixpc::BSR1_BASE + 1).mirror(unixpc::ZONE_WORD_MIRROR).r(FUNC(unixpc_state::bsr1_r));
	map(unixpc::TSR_BASE, unixpc::TSR_BASE + 1).mirror(unixpc::ZONE_WORD_MIRROR).r(FUNC(unixpc_state::tsr_r));
	map(unixpc::DMA_COUNT_BASE, unixpc::DMA_COUNT_BASE + 1).mirror(unixpc::ZONE_WORD_MIRROR).rw(FUNC(unixpc_state::diskdma_size_r), FUNC(unixpc_state::diskdma_size_w));
	map(unixpc::LPSR_BASE, unixpc::LPSR_BASE + 1).mirror(unixpc::ZONE_WORD_MIRROR).r(FUNC(unixpc_state::line_printer_r));
	map(unixpc::RTC_WRITE_BASE, unixpc::RTC_WRITE_BASE + 1).mirror(unixpc::ZONE_WORD_MIRROR).w(FUNC(unixpc_state::rtc_w));
	map(unixpc::TCR_BASE, unixpc::TCR_BASE + 1).select(unixpc::LATCH_STRIDE).w(FUNC(unixpc_state::tcr_w));
	// MCR is 16-bit; CLRSINT- is D15 on the even byte (D8-D15).
	map(unixpc::MCR_BASE, unixpc::MCR_BASE + 1).w("mreg", FUNC(output_latch_device::write)).umask16(unixpc::HI_BYTE);
	map(unixpc::DIALER_BASE, unixpc::DIALER_BASE + unixpc::DIALER_SIZE - 1).w(FUNC(unixpc_state::dialer_w));
	map(unixpc::CSR_BASE, unixpc::CSR_BASE + 1).mirror(unixpc::ZONE_WORD_MIRROR).rw(FUNC(unixpc_state::csr_r), FUNC(unixpc_state::csr_w));
	map(unixpc::DMA_PTR_BASE, unixpc::DMA_PTR_BASE + unixpc::DMA_PTR_SIZE - 1).w(FUNC(unixpc_state::diskdma_ptr_w));
	map(unixpc::DISK_CTRL_BASE, unixpc::DISK_CTRL_BASE).w(FUNC(unixpc_state::disk_control_w)).cswidth(16);
	// Word write. The low byte is the data latch. clr.w of $4F0000,
	// which is how the kernel drops LPINT+, is this same register.
	map(unixpc::PRINTER_BASE & ~1u, unixpc::PRINTER_BASE).mirror(unixpc::ZONE_WORD_MIRROR).w(FUNC(unixpc_state::printer_data_w));
	// 16K PROM only. Mirroring it through the top of the map ate FDC/HDC
	// reads: writes still reached the chips, but status came back as ROM.
	map(unixpc::PROM_BASE, unixpc::PROM_BASE + unixpc::PROM_SIZE - 1).rom().region("bootrom", 0);
	map(unixpc::HDC_BASE, unixpc::HDC_BASE + unixpc::HDC_REGS - 1).mirror(unixpc::HDC_BLOCK_MIRROR).rw(FUNC(unixpc_state::hdc_word_r), FUNC(unixpc_state::hdc_word_w));
	map(unixpc::FDC_BASE, unixpc::FDC_BASE + unixpc::FDC_REGS - 1).mirror(unixpc::FDC_BLOCK_MIRROR).rw(FUNC(unixpc_state::fdc16_r), FUNC(unixpc_state::fdc16_w));
	// Whole 64K block, and the $F2xxxx alias. Byte or word writes.
	map(unixpc::MCR2_BASE, unixpc::MCR2_BASE + 1).mirror(unixpc::ZONE_WORD_MIRROR).w(FUNC(unixpc_state::mcr2_w));
	map(unixpc::MCR2_ALIAS, unixpc::MCR2_ALIAS + 1).mirror(unixpc::ZONE_WORD_MIRROR).w(FUNC(unixpc_state::mcr2_w));
	map(unixpc::RTC_READ_BASE, unixpc::RTC_READ_BASE + 1).r(FUNC(unixpc_state::rtc_r));
	map(unixpc::GCR_BASE, unixpc::GCR_BASE + 1).select(unixpc::LATCH_STRIDE).w(FUNC(unixpc_state::gcr_w));
	map(unixpc::MPSC_BASE, unixpc::MPSC_BASE + unixpc::MPSC_REGS - 1).rw(FUNC(unixpc_state::mpsc_cd_r), FUNC(unixpc_state::mpsc_cd_w)).umask16(unixpc::LO_BYTE);
	map(unixpc::MODEM_BASE, unixpc::MODEM_BASE + unixpc::MODEM_SIZE - 1).rw(FUNC(unixpc_state::modem_r), FUNC(unixpc_state::modem_w));
	map(unixpc::KBC_BASE, unixpc::KBC_BASE + unixpc::KBC_REGS - 1).rw(m_kbc, FUNC(acia6850_device::read), FUNC(acia6850_device::write)).umask16(unixpc::HI_BYTE);
	// Eight expansion I/O windows. An empty slot completes the cycle
	// with 0xFF. A bus error here would abort the identity walk.
	map(unixpc::EXP_IO_BASE, unixpc::EXP_IO_BASE + unixpc::EXP_IO_WINDOWS * unixpc::EXP_IO_SIZE - 1).rw(FUNC(unixpc_state::exp_io_r), FUNC(unixpc_state::exp_io_w));
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
	// Default is the 1400/16/17 boot image: P5.1 field PAL and a WD2010.
	// The other three are the same limits, selected here. The image file
	// has to match the limits or the kernel will not mount it.
	PORT_START("DISKCFG")
	PORT_CONFNAME(0x01, 0x01, "Main board")
	PORT_CONFSETTING(0x00, "P3...P5")
	PORT_CONFSETTING(0x01, "P5.1")
	PORT_CONFNAME(0x02, 0x02, "Winchester chip")
	PORT_CONFSETTING(0x00, "WD1010")
	PORT_CONFSETTING(0x02, "WD2010")
	PORT_START("WINSCALE")
	PORT_CONFNAME(0x03, 0x01, "Window scale")
	PORT_CONFSETTING(0x00, "1.5x")
	PORT_CONFSETTING(0x01, "2x")
	PORT_CONFSETTING(0x02, "2.5x")
	PORT_CONFSETTING(0x03, "3x")
	// Off by default: RAM has no real parity bits. Turn on for the
	// Diagnostic Disk Memory & Parity "Reads a Parity Error" subtest
	// (BPPLUS poison writes + PIE read → level-7). Takes effect on reset.
	PORT_START("PARITY")
	PORT_CONFNAME(0x01, 0x00, "RAM parity emulation")
	PORT_CONFSETTING(0x00, DEF_STR(Off))
	PORT_CONFSETTING(0x01, DEF_STR(On))
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
	M68010(config, m_maincpu, 40_MHz_XTAL / unixpc::CPU_CLK_DIV);
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
	// Expansion. Ethernet selects level 1 or 5. The dual-EIA Z8530 is level 5.
	// One merger input per chassis slot, so the cards share the line.
	INPUT_MERGER_ANY_HIGH(config, m_int1);
	m_int1->output_handler().set_inputline(m_maincpu, M68K_IRQ_1);
	INPUT_MERGER_ANY_HIGH(config, m_int5);
	m_int5->output_handler().set_inputline(m_maincpu, M68K_IRQ_5);

	output_latch_device &mreg(OUTPUT_LATCH(config, "mreg"));
	mreg.bit_handler<0>().set_output("led_0").invert();
	mreg.bit_handler<1>().set_output("led_1").invert();
	mreg.bit_handler<2>().set_output("led_2").invert();
	mreg.bit_handler<3>().set_output("led_3").invert();
	// D12: 0 = modem channel clock from the modem, 1 = fixed 19.2 kHz.
	mreg.bit_handler<4>().set(FUNC(unixpc_state::modem_brg_select_w));
	// MCR D13 → cable STROBE* (active low). Board method gates the pulse
	// while busy is stretched so the generic printer cannot ACK twice.
	mreg.bit_handler<5>().set(FUNC(unixpc_state::printer_strobe_w)).invert();
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
	screen.set_raw(40_MHz_XTAL / unixpc::DOT_CLK_DIV, unixpc::SCREEN_HTOTAL, 0, unixpc::SCREEN_W, unixpc::SCREEN_VTOTAL, 0, unixpc::SCREEN_H);
	screen.set_physical_aspect(unixpc::SCREEN_W, unixpc::SCREEN_H);
	screen.set_palette("palette");
	screen.screen_vblank().set(FUNC(unixpc_state::vblank_w));
	// vsync should actually last 17264 pixels

	config.set_default_layout(layout_unixpc);

	PALETTE(config, "palette", palette_device::MONOCHROME);

	SPEAKER(config, "mono").front_center();
	BEEP(config, m_beep_lo, unixpc::DTMF_ROW_HZ[0]).add_route(ALL_OUTPUTS, "mono", 0.20);
	BEEP(config, m_beep_hi, unixpc::DTMF_COL_HZ[0]).add_route(ALL_OUTPUTS, "mono", 0.20);

	// internal ram
	RAM(config, RAM_TAG).set_default_size("4M").set_extra_options("512K,1M,2M,4M");

	// floppy
	WD2797(config, m_wd2797, 40_MHz_XTAL / unixpc::FDC_CLK_DIV); // 1PCK (CPU clock) divided by custom DMA chip
	m_wd2797->intrq_wr_callback().set(FUNC(unixpc_state::wd2797_intrq_w));
	m_wd2797->drq_wr_callback().set(FUNC(unixpc_state::wd2797_drq_w));
	FLOPPY_CONNECTOR(config, "wd2797:0", unixpc_floppies, "525dd", floppy_image_device::default_mfm_floppy_formats);

	WD1010(config, m_hdc, 40_MHz_XTAL / unixpc::HDC_CLK_DIV);
	m_hdc->out_intrq_callback().set(FUNC(unixpc_state::wd1010_intrq_w));
	m_hdc->out_bdrq_callback().set(FUNC(unixpc_state::hdc_bdrq_w));
	m_hdc->out_bcr_callback().set(FUNC(unixpc_state::hdc_bcr_w));
	m_hdc->out_data_callback().set(FUNC(unixpc_state::hdc_out_data));
	m_hdc->in_data_callback().set(FUNC(unixpc_state::hdc_in_data));
	HARDDISK(config, m_hdr0, 0);
	// Second image, selected by MCR2 D1 on a P5.1 board. Mount with -hard2.
	UNIXPC_HARD2(config, m_hdr1, 0);

	upd7201_device &mpsc(UPD7201(config, m_mpsc, 19.6608_MHz_XTAL / unixpc::MPSC_CLK_DIV));
	mpsc.out_txda_callback().set("rs232", FUNC(rs232_port_device::write_txd));
	mpsc.out_dtra_callback().set("rs232", FUNC(rs232_port_device::write_dtr));
	mpsc.out_rtsa_callback().set("rs232", FUNC(rs232_port_device::write_rts));
	mpsc.out_int_callback().set(FUNC(unixpc_state::mpsc_irq_w));

	// 838A baud generator output. Frequency is programmed by dialer_w.
	// 9600 (x16) until the dialer page programs another N. The configured
	// rate survives device_reset; a zero here leaves TxC stopped.
	clock_device &rs232_brg(CLOCK(config, m_rs232_brg, unixpc::rs232_clock_hz(2)));
	rs232_brg.signal_handler().set(FUNC(unixpc_state::rs232_clock_w));
	// Fixed 19.2 kHz for the modem channel, gated by MCR D12.
	clock_device &modem_brg(CLOCK(config, m_modem_brg, 0));
	modem_brg.signal_handler().set(FUNC(unixpc_state::modem_clock_w));

	ACIA6850(config, m_kbc);
	m_kbc->irq_handler().set_inputline(m_maincpu, M68K_IRQ_3);

	TC8250(config, m_rtc, 32.768_kHz_XTAL);

	rs232_port_device &rs232(RS232_PORT(config, "rs232", default_rs232_devices, nullptr));
	rs232.rxd_handler().set(m_mpsc, FUNC(upd7201_device::rxa_w));
	rs232.cts_handler().set(m_mpsc, FUNC(upd7201_device::ctsa_w));
	rs232.dcd_handler().set(m_mpsc, FUNC(upd7201_device::dcda_w));
	// 7201 has no DSR pin; boards commonly wire DSR into SYNCA (RR0).
	rs232.dsr_handler().set(m_mpsc, FUNC(upd7201_device::synca_w));
	// Empty slot: CTS/DCD forced ready in machine_reset. With
	// -rs232 loopback (or dec_loopback), the plug drives CTS/DCD/DSR.

	// Three chassis slots. -expN eia is the dual-EIA card (port1 is
	// channel A, port2 is channel B). -expN ethernet is the LANCE card.
	// Both stay empty unless selected, so a stock boot still sees no card.
	// The index is that slot's input on the shared expansion interrupt.
	UNIXPC_EXP(config, "exp0", unixpc_exp_cards, nullptr, 0);
	UNIXPC_EXP(config, "exp1", unixpc_exp_cards, nullptr, 1);
	UNIXPC_EXP(config, "exp2", unixpc_exp_cards, nullptr, 2);

	// Empty by default (status bits float high). For Diagnostic Disk
	// Parallel Printer (menu 10) and capture: -printer printer -prin file.prn
	centronics_device &printer(CENTRONICS(config, "printer", centronics_devices, nullptr));
	output_latch_device &printlatch(OUTPUT_LATCH(config, "printlatch"));
	printer.set_output_latch(printlatch);
	printer.busy_handler().set(FUNC(unixpc_state::printer_busy_w));
	printer.select_handler().set(FUNC(unixpc_state::printer_select_w));
	printer.perror_handler().set(FUNC(unixpc_state::printer_paper_w));
	printer.fault_handler().set(FUNC(unixpc_state::printer_fault_w));
	printer.ack_handler().set(FUNC(unixpc_state::printer_ack_w));
}



/***************************************************************************
    ROM DEFINITIONS
***************************************************************************/

// ROMs were provided by Michael Lee und imaged by Philip Pemberton
ROM_START( 3b1 )
	ROM_REGION16_BE(unixpc::PROM_SIZE, "bootrom", 0)
	ROM_LOAD16_BYTE("72-00617.15c", 0x000000, unixpc::ROM_CHIP_SIZE, CRC(4e93ff40) SHA1(1a97c8d32ec862f7f5fa1032f1688b76ea0672cc))
	ROM_LOAD16_BYTE("72-00616.14c", 0x000001, unixpc::ROM_CHIP_SIZE, CRC(c61f7ae0) SHA1(ab3ac29935a2a587a083c4d175a5376badd39058))
ROM_END


/***************************************************************************
    GAME DRIVERS
***************************************************************************/

//    YEAR  NAME  PARENT  COMPAT  MACHINE  INPUT   CLASS         INIT        COMPANY  FULLNAME             FLAGS
COMP( 1985, 3b1,  0,      0,      unixpc,  unixpc, unixpc_state, empty_init, "AT&T",  "UNIX PC Model 3B1", MACHINE_NOT_WORKING | MACHINE_SUPPORTS_SAVE )
