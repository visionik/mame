// license:GPL-2.0+
// copyright-holders:Dirk Best, R. Belmont, Visionik

#include "unixpc.h"

#include "bus/rs232/rs232.h"

// Machine: mapper, disks, keyboard, serial, and the status registers.

#define LOG_MAP (1U << 1)
#define LOG_HDC (1U << 2)

#define VERBOSE (0)
#include "logmacro.h"

ALLOW_SAVE_TYPE(unixpc::dma_source)

void unixpc::unixpc_save_dma(device_t &host, dma_engine &engine)
{
	host.save_item(engine.m_pointer, "dma.pointer");
	host.save_item(engine.m_count, "dma.count");
	host.save_item(engine.m_reading, "dma.reading");
	host.save_item(engine.m_enabled, "dma.enabled");
	host.save_item(engine.m_source, "dma.source");
}

void unixpc::unixpc_save_board(device_t &host, board_status &board)
{
	host.save_item(board.m_regs.genstat, "board.gsr");
	host.save_item(board.m_regs.bsr0, "board.bsr0");
	host.save_item(board.m_regs.bsr1, "board.bsr1");
	host.save_item(board.m_ee, "board.ee");
	host.save_item(board.m_pie, "board.pie");
	host.save_item(board.m_nmi_latched, "board.nmi");
	host.save_item(board.m_timer_latched, "board.timer");
	host.save_item(board.m_clrsint_high, "board.clrsint");
	host.save_item(board.m_status_frozen, "board.frozen");
	host.save_item(board.m_perr, "board.perr");
}

void unixpc::unixpc_save_kbd(device_t &host, kbd_queue &queue)
{
	host.save_item(queue.m_buf, "kbd.buf");
	host.save_item(queue.m_head, "kbd.head");
	host.save_item(queue.m_tail, "kbd.tail");
	host.save_item(queue.m_len, "kbd.len");
	host.save_item(queue.m_last_was_mouse, "kbd.mouse");
}

void unixpc::unixpc_save_mapper(device_t &host, mapper &map)
{
	host.save_item(map.m_vidpal, "map.vidpal");
}

DEFINE_DEVICE_TYPE(UNIXPC_EIA, unixpc_eia_device, "unixpc_eia", "UNIX PC Dual EIA")
DEFINE_DEVICE_TYPE(UNIXPC_ENET, unixpc_enet_device, "unixpc_enet", "UNIX PC Ethernet")
DEFINE_DEVICE_TYPE(UNIXPC_EXP, unixpc_exp_device, "unixpc_exp", "UNIX PC Expansion Slot")

namespace {

int exp_card_bit(device_t const &card)
{
	auto const *slot = dynamic_cast<unixpc_exp_device const *>(card.owner());
	if (slot == nullptr)
		return -1;
	return unixpc::exp_irq_bit(int(slot->slot_index()));
}

void exp_irq_w(input_merger_device &line, int bit, int state)
{
	switch (bit)
	{
	case 0: line.in_w<0>(state); break;
	case 1: line.in_w<1>(state); break;
	case 2: line.in_w<2>(state); break;
	default: break;
	}
}

} // namespace

unixpc_eia_device::unixpc_eia_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: unixpc_card_device(mconfig, UNIXPC_EIA, tag, owner, clock)
	, m_scc(*this, "scc")
	, m_irq(*this, ":int5")
{
}

void unixpc_eia_device::irq_w(int state)
{
	exp_irq_w(*m_irq, exp_card_bit(*this), state);
}

uint8_t unixpc_eia_device::low_byte(uint16_t data, uint16_t mem_mask) const
{
	if (mem_mask & unixpc::LO_BYTE)
		return uint8_t(data);
	return uint8_t(data >> 8);
}

uint16_t unixpc_eia_device::exp_r(uint32_t offset)
{
	int const id = unixpc::exp_id_byte(unixpc::EIA_ID, offset);
	if (id >= 0)
		return uint16_t(0xff00 | id);
	uint8_t value = 0xff;
	switch (unixpc::eia_decode(offset))
	{
	case unixpc::eia_reg::b_ctrl: value = m_scc->cb_r(0); break;
	case unixpc::eia_reg::a_ctrl: value = m_scc->ca_r(0); break;
	case unixpc::eia_reg::b_data: value = m_scc->db_r(0); break;
	case unixpc::eia_reg::a_data: value = m_scc->da_r(0); break;
	default: break;
	}
	return uint16_t(0xff00 | value);
}

void unixpc_eia_device::exp_w(uint32_t offset, uint16_t data, uint16_t mem_mask)
{
	if (unixpc::exp_reset_write(offset))
	{
		m_scc->reset();
		return;
	}
	uint8_t const value = low_byte(data, mem_mask);
	switch (unixpc::eia_decode(offset))
	{
	case unixpc::eia_reg::b_ctrl: m_scc->cb_w(0, value); break;
	case unixpc::eia_reg::a_ctrl: m_scc->ca_w(0, value); break;
	case unixpc::eia_reg::b_data: m_scc->db_w(0, value); break;
	case unixpc::eia_reg::a_data: m_scc->da_w(0, value); break;
	default: break;
	}
}

void unixpc_eia_device::device_add_mconfig(machine_config &config)
{
	// Same clock as the built-in 7201: 19.6608 MHz / 8.
	scc8530_device &scc(SCC8530(config, "scc", 19.6608_MHz_XTAL / unixpc::MPSC_CLK_DIV));
	scc.out_int_callback().set(FUNC(unixpc_eia_device::irq_w));
	scc.out_txda_callback().set("port1", FUNC(rs232_port_device::write_txd));
	scc.out_dtra_callback().set("port1", FUNC(rs232_port_device::write_dtr));
	scc.out_rtsa_callback().set("port1", FUNC(rs232_port_device::write_rts));
	scc.out_txdb_callback().set("port2", FUNC(rs232_port_device::write_txd));
	scc.out_dtrb_callback().set("port2", FUNC(rs232_port_device::write_dtr));
	scc.out_rtsb_callback().set("port2", FUNC(rs232_port_device::write_rts));

	rs232_port_device &port1(RS232_PORT(config, "port1", default_rs232_devices, nullptr));
	port1.rxd_handler().set("scc", FUNC(scc8530_device::rxa_w));
	port1.dcd_handler().set("scc", FUNC(scc8530_device::dcda_w));
	port1.cts_handler().set("scc", FUNC(scc8530_device::ctsa_w));
	port1.dsr_handler().set("scc", FUNC(scc8530_device::synca_w));

	rs232_port_device &port2(RS232_PORT(config, "port2", default_rs232_devices, nullptr));
	port2.rxd_handler().set("scc", FUNC(scc8530_device::rxb_w));
	port2.dcd_handler().set("scc", FUNC(scc8530_device::dcdb_w));
	port2.cts_handler().set("scc", FUNC(scc8530_device::ctsb_w));
	port2.dsr_handler().set("scc", FUNC(scc8530_device::syncb_w));
}

unixpc_enet_device::unixpc_enet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: unixpc_card_device(mconfig, UNIXPC_ENET, tag, owner, clock)
	, m_lance(*this, "lance")
	, m_irq(*this, ":int1")
	, m_irq5(*this, ":int5")
	, m_cpu(*this, ":maincpu")
	, m_mac{ 0x02, 0x00, 0x00, 0x3b, 0x00, 0x01 }
{
}

void unixpc_enet_device::device_start()
{
	save_item(NAME(m_mac));
	save_item(NAME(m_lance_level));
}

void unixpc_enet_device::device_reset()
{
	m_board.reset();
	m_lance_level = false;
	publish_irq();
}

void unixpc_enet_device::publish_irq()
{
	int const bit = exp_card_bit(*this);
	int const asserted = m_board.irq() ? 1 : 0;
	if (m_board.interrupt_level_1())
	{
		exp_irq_w(*m_irq, bit, asserted);
		exp_irq_w(*m_irq5, bit, 0);
	}
	else
	{
		exp_irq_w(*m_irq, bit, 0);
		exp_irq_w(*m_irq5, bit, asserted);
	}
}

void unixpc_enet_device::irq_w(int state)
{
	bool const level = state != 0;
	if (level && !m_lance_level)
		m_board.raise_irq();
	m_lance_level = level;
	publish_irq();
}

u16 unixpc_enet_device::dma_r(offs_t offset)
{
	auto &host = *downcast<unixpc_state *>(&machine().root_device());
	uint32_t word = 0;
	if (!host.ram_word(uint32_t(offset) & ~1u, false, word))
		return 0xffff;
	return host.m_ramptr[word];
}

void unixpc_enet_device::dma_w(offs_t offset, u16 data)
{
	auto &host = *downcast<unixpc_state *>(&machine().root_device());
	uint32_t word = 0;
	if (host.ram_word(uint32_t(offset) & ~1u, true, word))
		host.m_ramptr[word] = data;
}

uint16_t unixpc_enet_device::exp_r(uint32_t offset)
{
	int const id = unixpc::exp_id_byte(unixpc::ENET_ID, offset);
	if (id >= 0)
		return uint16_t(0xff00 | id);
	int const octet = unixpc::enet_mac_octet(offset);
	if (octet >= 0)
		return uint16_t(0xff00 | m_mac[octet]);
	address_space &space = m_cpu->space(AS_PROGRAM);
	if (offset == unixpc::ENET_RDP)
		return m_lance->regs_r(space, 0);
	if (offset == unixpc::ENET_RAP)
		return m_lance->regs_r(space, 1);
	if (offset == unixpc::ENET_CTL)
	{
		uint16_t const value = m_board.read_control();
		publish_irq();
		return value;
	}
	if (offset == unixpc::ENET_LATCH)
		return m_board.latch();
	return unixpc::IO_OPEN;
}

void unixpc_enet_device::exp_w(uint32_t offset, uint16_t data, uint16_t mem_mask)
{
	if (unixpc::exp_reset_write(offset))
	{
		reset();
		return;
	}
	if (offset == unixpc::ENET_RDP)
		m_lance->regs_w(0, data);
	else if (offset == unixpc::ENET_RAP)
		m_lance->regs_w(1, data);
	else if (offset == unixpc::ENET_CTL)
	{
		m_board.write_control(data);
		publish_irq();
	}
	else if (offset == unixpc::ENET_LATCH)
	{
		m_board.write_ring(data);
		publish_irq();
	}
	(void)mem_mask;
}

void unixpc_enet_device::device_add_mconfig(machine_config &config)
{
	am7990_device &lance(AM7990(config, "lance", 10_MHz_XTAL));
	lance.intr_out().set(FUNC(unixpc_enet_device::irq_w));
	lance.dma_in().set(FUNC(unixpc_enet_device::dma_r));
	lance.dma_out().set(FUNC(unixpc_enet_device::dma_w));
}

unixpc_exp_device::unixpc_exp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, UNIXPC_EXP, tag, owner, clock)
	, device_slot_interface(mconfig, *this)
{
}

unixpc_exp_device::unixpc_exp_device(const machine_config &mconfig, const char *tag, device_t *owner,
	void (*opts)(device_slot_interface &), const char *dflt, unsigned index)
	: unixpc_exp_device(mconfig, tag, owner, 0)
{
	m_slot = index;
	option_reset();
	(*opts)(*this);
	set_default_option(dflt);
	set_fixed(false);
}

void unixpc_exp_cards(device_slot_interface &device)
{
	device.option_add("eia", UNIXPC_EIA);
	device.option_add("ethernet", UNIXPC_ENET);
}


unixpc_card_device *unixpc_state::exp_card(unsigned window) const
{
	if (window >= 3)
		return nullptr;
	return dynamic_cast<unixpc_card_device *>(m_exp[window]->get_card_device());
}

uint16_t unixpc_state::exp_io_r(offs_t offset)
{
	uint32_t const byte = uint32_t(offset) << 1;
	unsigned const window = byte / unixpc::EXP_IO_SIZE;
	unixpc_card_device *card = exp_card(window);
	if (card == nullptr)
		return unixpc::IO_OPEN;
	return card->exp_r(byte % unixpc::EXP_IO_SIZE);
}

void unixpc_state::exp_io_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	uint32_t const byte = uint32_t(offset) << 1;
	unsigned const window = byte / unixpc::EXP_IO_SIZE;
	unixpc_card_device *card = exp_card(window);
	if (card != nullptr)
		card->exp_w(byte % unixpc::EXP_IO_SIZE, data, mem_mask);
}

uint16_t unixpc_state::modem_r(offs_t offset)
{
	// 882A. RR2 at +0x2000: 0xFF means no modem. There is no phone-line
	// path on this board; channel B only has its baud clock. Empty I/O
	// is all-ones.
	uint32_t const a = uint32_t(offset) << 1;
	if ((a & 0xf000) == unixpc::MODEM_RR2)
		return unixpc::MODEM_ABSENT;
	return unixpc::IO_OPEN;
}


void unixpc_state::modem_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	(void)offset;
	(void)data;
	(void)mem_mask;
}


void unixpc_state::rs232_clock_w(int state)
{
	m_mpsc->rxca_w(state);
	m_mpsc->txca_w(state);
}


uint8_t unixpc_state::mpsc_cd_r(offs_t offset)
{
	uint8_t const data = m_mpsc->cd_ba_r(offset);
	static int n;
	if (n < 48)
	{
		++n;
		osd_printf_info("mpsc r off=%u data=%02x\n", unsigned(offset), data);
	}
	return data;
}


void unixpc_state::mpsc_cd_w(offs_t offset, uint8_t data)
{
	static int n;
	if (n < 48)
	{
		++n;
		osd_printf_info("mpsc w off=%u data=%02x\n", unsigned(offset), data);
	}
	m_mpsc->cd_ba_w(offset, data);
}


void unixpc_state::modem_clock_w(int state)
{
	m_mpsc->rxcb_w(state);
	m_mpsc->txcb_w(state);
}


void unixpc_state::modem_brg_select_w(int state)
{
	// MCR D12: 1 = fixed 19.2 kHz into the modem channel, 0 = modem clock.
	m_modem_brg->set_clock(state ? unixpc::MODEM_UART_CLOCK : 0);
}


void unixpc_state::mpsc_irq_w(int state)
{
	m_mpsc_irq = (state != 0);
	update_mpsc_irq();
}


void unixpc_state::update_mpsc_irq()
{
	// The PROM RAM test stores each address into itself, so vector 0x70
	// holds 0x00000070 until the PROM installs a real table. Hold IRQ4
	// off while the CPU is still executing from that ROM.
	uint32_t const pc = m_maincpu->pc();
	bool const prom = (pc >= unixpc::PROM_BASE) && (pc < unixpc::PROM_BASE + unixpc::PROM_SIZE);
	m_maincpu->set_input_line(M68K_IRQ_4, (!prom && m_mpsc_irq) ? ASSERT_LINE : CLEAR_LINE);
}

/***************************************************************************
    MEMORY
***************************************************************************/

void unixpc_state::gcr_w(offs_t offset, uint16_t data)
{
	m_gcr->write_bit(offset >> unixpc::LATCH_ADDR_SHIFT, BIT(data, unixpc::GCR_DATA_BIT));
}


uint16_t unixpc_state::mapram_r(offs_t offset)
{
	return m_mapram[offset];
}


void unixpc_state::mapram_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	uint16_t const old = m_mapram[offset];
	COMBINE_DATA(&m_mapram[offset]);
	if (old != m_mapram[offset])
		LOGMASKED(LOG_MAP, "map[%03x] %04x->%04x pc=%06x\n",
			unsigned(offset), old, m_mapram[offset], unsigned(m_maincpu->pc()));
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


void unixpc_state::enforce_user_highmem(offs_t addr, uint16_t mem_mask, bool write)
{
	if (machine().side_effects_disabled())
		return;

	u16 const fc = m_maincpu->get_fc();
	bool const super = (fc == 0) || (fc >= 4);
	if (super)
		return;

	// FreeBee: VIDPAL lets user touch the frame buffer window.
	if (m_mapper.vidpal()
		&& addr >= unixpc::VRAM_BASE
		&& addr < unixpc::VRAM_BASE + unixpc::VRAM_SIZE)
		return;

	uint32_t fault_addr = uint32_t(addr);
	if (mem_mask == unixpc::LO_BYTE)
		fault_addr |= 1;
	apply_cpu_fault(unixpc::access::privileged, fault_addr, write);
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
	uint32_t word = 0;
	if (q.result != unixpc::access::ok || !ram_word(q.physical, false, word))
		return unixpc::IO_OPEN;
	return m_ramptr[word];
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
		uint32_t word = 0;
		if (q.result == unixpc::access::ok && ram_word(q.physical, false, word))
		{
			uint16_t const w = m_ramptr[word];
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

uint16_t unixpc_state::ram_mmu_r(offs_t offset, uint16_t mem_mask)
{
	uint32_t const logical = uint32_t(offset) << 1;
	// 16-bit map offset is always even; odd-byte cycles select LO_BYTE.
	uint32_t const byte_logical = (mem_mask == unixpc::LO_BYTE) ? (logical | 1) : logical;
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
		apply_cpu_fault(p.result, byte_logical, false);
		return unixpc::IO_OPEN;
	}
	uint32_t ram_word_index = 0;
	if (!ram_word(p.physical, false, ram_word_index))
		return unixpc::IO_OPEN;

#if UNIXPC_USER_TRACE
	if (!machine().side_effects_disabled())
		maybe_user_trace(fc, logical, p, false);
#endif
#if UNIXPC_BERR_TRACE
	if (!machine().side_effects_disabled())
		maybe_berr_kpc(fc, logical);
#endif
	// Forced bad-parity path (Machine Configuration / -parity).
	// Cheap when off, PIE clear, or poison empty. NMI is not EE-gated:
	// the diag turns PIE on for the read and leaves EE alone.
	if (m_parity_emul && m_board.pie() && !m_poison.empty()
		&& !machine().side_effects_disabled()
		&& m_poison.find(ram_word_index) != m_poison.end())
	{
		int const width = (m_maincpu->get_access_sz() == 1) ? 8 : 16;
		auto const enc = unixpc::encode_parity_fault(false, byte_logical, width, true);
		bool const raised = m_board.latch_parity(enc);
		if (raised)
			nmi_update();
		LOG("parity fault addr=%06x phys=%06x ee=%d pc=%06x\n",
			byte_logical, p.physical, int(m_board.ee()), unsigned(m_maincpu->pc()));
	}
	return m_ramptr[ram_word_index];
}


void unixpc_state::ram_mmu_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	uint32_t const logical = uint32_t(offset) << 1;
	uint32_t const byte_logical = (mem_mask == unixpc::LO_BYTE) ? (logical | 1) : logical;
	u16 const fc = m_maincpu->get_fc();
	bool const super = (fc == 0) || (fc >= 4);
	unixpc::probe const p = machine().side_effects_disabled()
		? m_mapper.check(logical, true, super, false)
		: m_mapper.translate(logical, true, super, false);
	if (p.result != unixpc::access::ok)
	{
		apply_cpu_fault(p.result, byte_logical, true);
		return;
	}
	uint32_t ram_word_index = 0;
	if (ram_word(p.physical, true, ram_word_index))
	{
		COMBINE_DATA(&m_ramptr[ram_word_index]);
		if (m_parity_emul && !machine().side_effects_disabled())
		{
			if (m_bpplus)
				m_poison.insert(ram_word_index);
			else if (!m_poison.empty())
				m_poison.erase(ram_word_index);
		}
	}
}


uint16_t unixpc_state::bootrom_mirror_r(offs_t offset)
{
	uint16_t const *const rom = &m_bootrom->as_u16(0);
	unsigned const words = unsigned(m_bootrom->bytes() / 2);
	unsigned const live = unixpc::PROM_MIRROR_BYTES / 2;
	(void)words;
	return rom[offset & (live - 1)];
}


void unixpc_state::ram_phys_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	// ROM overlay: reads come from the PROM, writes go to physical RAM.
	uint32_t ram_word_index = 0;
	if (ram_word(uint32_t(offset) << 1, true, ram_word_index))
		COMBINE_DATA(&m_ramptr[ram_word_index]);
}

bool unixpc_state::ram_word(uint32_t phys, bool write, uint32_t &word) const
{
	unixpc::ram_sizes const sz = unixpc::ram_installed(m_ramsize);
	unixpc::ram_ref const ref = unixpc::ram_access(phys, sz.board, sz.expansion, write);
	if (!ref.hit || m_ramptr == nullptr || ref.storage >= m_ramsize)
		return false;
	word = ref.storage >> 1;
	return true;
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
	uint32_t ram_word_index = 0;
	if (ram_word(p.physical, true, ram_word_index))
		m_ramptr[ram_word_index] = word;
	m_dma.advance();
	return true;
}


uint16_t unixpc_state::dma_fetch_word()
{
	if (!m_dma.wants_transfer())
		return unixpc::IO_OPEN;

	uint32_t const logical = m_dma.pointer();
	unixpc::probe const p = m_mapper.translate(logical, false, true, true);
	if (p.result != unixpc::access::ok)
	{
		if (m_board.latch_dma(unixpc::encode_dma_fault(true, logical, m_board.pie())))
			nmi_update();
		return unixpc::IO_OPEN;
	}
	uint32_t ram_word_index = 0;
	uint16_t const word = ram_word(p.physical, false, ram_word_index) ? m_ramptr[ram_word_index] : uint16_t(0xffff);
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
		// Write path fetches a 16-bit DMA word on the high-byte DRQ. That
		// fetch can retire the DMA count while the low byte is still
		// pending — the next (final) DRQ must still deliver that byte.
		// Draining with data_r() left the previous data-reg value on disk
		// (surface test: last byte F7 instead of 80 → FFFFF7F7 vs FFFFF780).
		if (m_dma.reading() && m_fdc_have_lo && m_fdc_drq)
		{
			m_wd2797->data_w(m_fdc_pending_lo);
			m_fdc_have_lo = false;
			return;
		}
		// Still have to retire DRQ or the 2797 never interrupts.
		if (m_fdc_drq)
			(void)m_wd2797->data_r();
		return;
	}

	if (m_dma.reading())
	{
		// One 2797 DRQ is one byte. The board DMA engine is 16-bit, so we
		// split each fetched word across two DRQs (high byte, then low).
		// Writing both bytes in one DRQ overwrote the pending format byte
		// and left post-format Write Sector returning RNF on the VHB.
		uint8_t b;
		if (!m_fdc_have_lo)
		{
			uint16_t const word = dma_fetch_word();
			b = uint8_t(word >> 8);
			m_fdc_pending_lo = uint8_t(word);
			m_fdc_have_lo = true;
		}
		else
		{
			b = m_fdc_pending_lo;
			m_fdc_have_lo = false;
		}
#if UNIXPC_FDC_TRACE
		if (m_fdc_sec_bytes == 0)
			m_fdc_sec_ptr = m_dma.pointer();
		if (m_fdc_sec_bytes < 8)
			m_fdc_sec_head[m_fdc_sec_bytes] = b;
		m_fdc_sec_bytes++;
#endif
		m_wd2797->data_w(b);
#if UNIXPC_FDC_TRACE
		if (m_fdc_sec_bytes >= 512)
		{
			static FILE *wtf;
			if (wtf == nullptr)
			{
				wtf = fopen("/tmp/mame_fdc_write.tsv", "w");
				if (wtf != nullptr)
					fprintf(wtf, "n\tdir\ttrk\tsec\tside\tptr\tb0\tb1\tb2\tb3\tb4\tb5\tb6\tb7\n");
			}
			if (wtf != nullptr)
			{
				fprintf(wtf, "%d\tW\t%u\t%u\t%d\t%06x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\n",
					m_fdc_trace_n++,
					unsigned(m_wd2797->track_r()),
					unsigned(m_wd2797->sector_r()),
					int(m_floppy->ss_r()),
					m_fdc_sec_ptr,
					m_fdc_sec_head[0], m_fdc_sec_head[1], m_fdc_sec_head[2], m_fdc_sec_head[3],
					m_fdc_sec_head[4], m_fdc_sec_head[5], m_fdc_sec_head[6], m_fdc_sec_head[7]);
				fflush(wtf);
			}
			m_fdc_sec_bytes = 0;
		}
#endif
	}
	else
	{
		// One 2797 DRQ is one byte. The board DMA engine is 16-bit, so we
		// pack two successive DRQs into a word.
		uint8_t const b = m_wd2797->data_r();
#if UNIXPC_FDC_TRACE
		if (m_fdc_sec_bytes == 0)
			m_fdc_sec_ptr = m_dma.pointer();
		if (m_fdc_sec_bytes < 8)
			m_fdc_sec_head[m_fdc_sec_bytes] = b;
		m_fdc_sec_bytes++;
#endif
		if (!m_fdc_have_hi)
		{
			m_fdc_hi = b;
			m_fdc_have_hi = true;
		}
		else
		{
			(void)dma_store_word(uint16_t((uint16_t(m_fdc_hi) << 8) | b));
			m_fdc_have_hi = false;
#if UNIXPC_FDC_TRACE
			if (m_fdc_sec_bytes >= 512)
			{
				static FILE *tf;
				if (tf == nullptr)
				{
					tf = fopen("/tmp/mame_fdc.tsv", "w");
					if (tf != nullptr)
						fprintf(tf, "n\tdir\ttrk\tsec\tside\tptr\tphys\tb0\tb1\tb2\tb3\tb4\tb5\tb6\tb7\tr0\tr1\tr2\tr3\tr4\tr5\tr6\tr7\n");
				}
				if (tf != nullptr)
				{
					auto cpu_byte = [this](uint32_t la) -> uint8_t
					{
						unixpc::probe const q = m_mapper.translate(la, false, true, true);
						uint32_t wi = 0;
						if (q.result != unixpc::access::ok || !ram_word(q.physical, false, wi))
							return 0xff;
						uint16_t const w = m_ramptr[wi];
						return (la & 1) ? uint8_t(w) : uint8_t(w >> 8);
					};
					auto cpu_fill = [&](uint32_t base, uint8_t *dst, int nbytes)
					{
						for (int i = 0; i < nbytes; i++)
							dst[i] = cpu_byte(base + uint32_t(i));
					};

					uint8_t const *const bfdc = m_fdc_sec_head;
					uint8_t ram8[8];
					uint32_t phys0 = 0xffffffffu;
					cpu_fill(m_fdc_sec_ptr, ram8, 8);
					unixpc::probe const p0 = m_mapper.translate(m_fdc_sec_ptr, false, true, true);
					if (p0.result == unixpc::access::ok)
						phys0 = p0.physical;

					uint8_t const trk = m_wd2797->track_r();
					uint8_t const sec = m_wd2797->sector_r();
					int const side = int(m_floppy->ss_r());
					s32 const n = m_fdc_trace_n++;

					fprintf(tf, "%d\tR\t%u\t%u\t%d\t%06x\t%06x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\n",
						n, unsigned(trk), unsigned(sec), side,
						m_fdc_sec_ptr, phys0,
						bfdc[0], bfdc[1], bfdc[2], bfdc[3], bfdc[4], bfdc[5], bfdc[6], bfdc[7],
						ram8[0], ram8[1], ram8[2], ram8[3], ram8[4], ram8[5], ram8[6], ram8[7]);
					fflush(tf);

					// Full-sector dump when this looks like the surface-test
					// walking pattern (word0 = FFFFF7xx).
					if (ram8[0] == 0xff && ram8[1] == 0xff && ram8[2] == 0xf7)
					{
						uint8_t secbuf[512];
						cpu_fill(m_fdc_sec_ptr, secbuf, 512);
						uint32_t const base = (uint32_t(secbuf[0]) << 24) | (uint32_t(secbuf[1]) << 16)
							| (uint32_t(secbuf[2]) << 8) | uint32_t(secbuf[3]);
						int first_bad = -1;
						uint32_t bad_got = 0, bad_exp = 0;
						for (int wi = 0; wi < 128; wi++)
						{
							uint8_t const *const p = secbuf + wi * 4;
							uint32_t const got = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
								| (uint32_t(p[2]) << 8) | uint32_t(p[3]);
							uint32_t const exp = base - uint32_t(wi);
							if (got != exp && first_bad < 0)
							{
								first_bad = wi;
								bad_got = got;
								bad_exp = exp;
							}
						}
						uint32_t const last = (uint32_t(secbuf[508]) << 24) | (uint32_t(secbuf[509]) << 16)
							| (uint32_t(secbuf[510]) << 8) | uint32_t(secbuf[511]);
						static FILE *sf;
						if (sf == nullptr)
						{
							sf = fopen("/tmp/mame_fdc_secdump.tsv", "w");
							if (sf != nullptr)
								fprintf(sf, "n\ttrk\tsec\tside\tptr\tbase\tfirst_bad\tbad_got\tbad_exp\tlast\n");
						}
						if (sf != nullptr)
						{
							fprintf(sf, "%d\t%u\t%u\t%d\t%06x\t%08x\t%d\t%08x\t%08x\t%08x\n",
								n, unsigned(trk), unsigned(sec), side, m_fdc_sec_ptr,
								base, first_bad, bad_got, bad_exp, last);
							fflush(sf);
						}
					}

					// Paired re-read: same CHS, different DMA pointer. Dump
					// both CPU-visible 512-byte buffers and hunt for the
					// diagnostic Expected/Received constants.
					for (int h = m_fdc_hist_count - 1; h >= 0; --h)
					{
						if (m_fdc_hist_trk[h] != trk || m_fdc_hist_sec[h] != sec || m_fdc_hist_side[h] != uint8_t(side))
							continue;
						if (m_fdc_hist_ptr[h] == m_fdc_sec_ptr)
							continue;

						uint32_t const ptr_a = m_fdc_hist_ptr[h];
						uint32_t const ptr_b = m_fdc_sec_ptr;
						s32 const n_a = m_fdc_hist_n[h];
						uint8_t buf_a[512], buf_b[512];
						cpu_fill(ptr_a, buf_a, 512);
						cpu_fill(ptr_b, buf_b, 512);

						static FILE *rf;
						if (rf == nullptr)
						{
							rf = fopen("/tmp/mame_fdc_reread.tsv", "w");
							if (rf != nullptr)
								fprintf(rf, "n_a\tn_b\ttrk\tsec\tside\tptr_a\tptr_b\tfirst_diff\tword_a\tword_b\tpat_a\tpat_b\tw0_a\tw1_a\tw2_a\tw3_a\tw0_b\tw1_b\tw2_b\tw3_b\n");
						}
						if (rf != nullptr)
						{
							int first_diff = -1;
							for (int i = 0; i < 512; i++)
							{
								if (buf_a[i] != buf_b[i])
								{
									first_diff = i;
									break;
								}
							}
							auto be32 = [](uint8_t const *p) -> uint32_t
							{
								return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
							};
							uint32_t const wa = (first_diff >= 0 && first_diff <= 508) ? be32(buf_a + (first_diff & ~3)) : be32(buf_a);
							uint32_t const wb = (first_diff >= 0 && first_diff <= 508) ? be32(buf_b + (first_diff & ~3)) : be32(buf_b);

							static uint32_t const pats[] = {
								0xfffff7ffu, 0xfffff3ffu, 0xffffefffu, 0xfffffffeu, 0xfefffffeu
							};
							auto find_pats = [&](uint8_t const *buf) -> int
							{
								int mask = 0;
								for (int pi = 0; pi < 5; pi++)
								{
									uint8_t be[4] = {
										uint8_t(pats[pi] >> 24), uint8_t(pats[pi] >> 16),
										uint8_t(pats[pi] >> 8), uint8_t(pats[pi])
									};
									uint8_t le[4] = { be[3], be[2], be[1], be[0] };
									for (int i = 0; i <= 508; i++)
									{
										if (!memcmp(buf + i, be, 4) || !memcmp(buf + i, le, 4))
										{
											mask |= (1 << pi);
											break;
										}
									}
								}
								return mask;
							};

							fprintf(rf, "%d\t%d\t%u\t%u\t%d\t%06x\t%06x\t%d\t%08x\t%08x\t%d\t%d\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\n",
								n_a, n, unsigned(trk), unsigned(sec), side,
								ptr_a, ptr_b, first_diff, wa, wb,
								find_pats(buf_a), find_pats(buf_b),
								be32(buf_a), be32(buf_a + 4), be32(buf_a + 8), be32(buf_a + 12),
								be32(buf_b), be32(buf_b + 4), be32(buf_b + 8), be32(buf_b + 12));
							fflush(rf);
						}
						break; // only pair with most recent prior hit
					}

					int w;
					if (m_fdc_hist_count < FDC_HIST)
					{
						w = m_fdc_hist_count++;
					}
					else
					{
						for (int i = 1; i < FDC_HIST; i++)
						{
							m_fdc_hist_trk[i - 1] = m_fdc_hist_trk[i];
							m_fdc_hist_sec[i - 1] = m_fdc_hist_sec[i];
							m_fdc_hist_side[i - 1] = m_fdc_hist_side[i];
							m_fdc_hist_ptr[i - 1] = m_fdc_hist_ptr[i];
							m_fdc_hist_n[i - 1] = m_fdc_hist_n[i];
						}
						w = FDC_HIST - 1;
					}
					m_fdc_hist_trk[w] = trk;
					m_fdc_hist_sec[w] = sec;
					m_fdc_hist_side[w] = uint8_t(side);
					m_fdc_hist_ptr[w] = m_fdc_sec_ptr;
					m_fdc_hist_n[w] = n;
				}
				m_fdc_sec_bytes = 0;
			}
#endif
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
			m_hdc_log_lba, unixpc::crc32_ieee(m_hdc_buf, unixpc::SECTOR_BYTES));
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
			if (m_hdc_dma_idx + 1 < unixpc::SECTOR_BYTES)
			{
				m_hdc_buf[m_hdc_dma_idx] = uint8_t(word >> 8);
				m_hdc_buf[m_hdc_dma_idx + 1] = uint8_t(word);
			}
		}
		else if (m_hdc_dma_idx + 1 < unixpc::SECTOR_BYTES)
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
    DMA
***************************************************************************/

uint16_t unixpc_state::diskdma_size_r()
{
	// PROM busy-waits until this register differs from the value it
	// loaded (two's complement word count | DMAEN). Echo enable/dir as
	// written; forcing bits 15-14 high made a 512-byte load look like it
	// never started and hung the wait.
	uint16_t data = uint16_t(m_dma.count() & unixpc::dma_engine::COUNT_MASK);
	if (m_dma.enabled())
		data |= unixpc::DMA_REG_ENABLE;
	if (m_dma.reading())
		data |= unixpc::DMA_REG_READ;
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
	m_dma.set_count(uint16_t(value & unixpc::dma_engine::COUNT_MASK));
	m_dma.set_enabled(BIT(value, 15));
	m_dma.set_reading(BIT(value, 14));
#if UNIXPC_FDC_TRACE
	// New DMA program: start a fresh sector capture.
	m_fdc_sec_bytes = 0;
#endif
	// Direction change / new count: drop any half-word in flight.
	m_fdc_have_hi = false;
	m_fdc_have_lo = false;
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
	if (addr & unixpc::DMA_PTR_HI_SEL)
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
	m_hdc_head_low = uint8_t(data & unixpc::DCTL_HEAD_MASK);
	hdc_apply_head();

	bool const hd_sel = BIT(data, unixpc::DCTL_HD_SEL);
	bool const fd_sel = BIT(data, unixpc::DCTL_FD_SEL);

	if (!BIT(data, unixpc::DCTL_HDC_RESET))
		m_hdc->reset();

	// DRDY is a drive pin, not the select bit. Apply after reset so a
	// kernel master-reset pulse does not leave READY clear.
	m_hdc->drdy_w(m_hdr0->exists());
	m_hdc->sc_w(1);

	m_floppy->mon_w(!BIT(data, unixpc::DCTL_MOTOR));

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
#if UNIXPC_FDC_TRACE
	// Do not status_r() here — reading status clears INTRQ and breaks boot.
	if (state && !m_fdc_intrq)
	{
		static FILE *irqf;
		static int irqn;
		if (irqf == nullptr)
		{
			irqf = fopen("/tmp/mame_fdc_irq.tsv", "w");
			if (irqf != nullptr)
				fprintf(irqf, "n\ttrk\tsec\tside\n");
		}
		if (irqf != nullptr)
		{
			fprintf(irqf, "%d\t%u\t%u\t%d\n",
				irqn++,
				unsigned(m_wd2797->track_r()),
				unsigned(m_wd2797->sector_r()),
				int(m_floppy->exists() ? m_floppy->ss_r() : -1));
			fflush(irqf);
		}
	}
#endif
	m_fdc_intrq = state != 0;
	if (m_fdc_intrq)
		m_fdc_irq_latch = true;
	m_int02->in_w<1>(state);
}


uint8_t unixpc_state::fdc_r(offs_t offset)
{
	// Empty drive: the boot ROM polls status after it resets the controller.
	// Not Ready has to still be set then, or the ROM decides a floppy is in.
	if (!m_floppy->exists() && (offset & 3) == 0)
	{
		m_fdc_irq_latch = false;
		m_fdc_intrq = false;
		m_int02->in_w<1>(0);
		return unixpc::FDC_NOT_READY;
	}
	uint8_t const v = m_wd2797->read(offset);
	if ((offset & 3) == 0)
	{
		m_fdc_irq_latch = false;
#if UNIXPC_FDC_TRACE
		static FILE *stf;
		static int stn;
		if (stf == nullptr)
		{
			stf = fopen("/tmp/mame_fdc_status.tsv", "w");
			if (stf != nullptr)
				fprintf(stf, "n\tstatus\ttrk\tsec\tside\n");
		}
		if (stf != nullptr)
		{
			fprintf(stf, "%d\t%02x\t%u\t%u\t%d\n",
				stn++,
				unsigned(v),
				unsigned(m_wd2797->track_r()),
				unsigned(m_wd2797->sector_r()),
				int(m_floppy->ss_r()));
			fflush(stf);
		}
#endif
	}
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
#if UNIXPC_FDC_TRACE
	if ((offset & 3) == 0)
	{
		static FILE *cmdf;
		static int cmdn;
		if (cmdf == nullptr)
		{
			cmdf = fopen("/tmp/mame_fdc_cmd.tsv", "w");
			if (cmdf != nullptr)
				fprintf(cmdf, "n\tcmd\ttrk\tsec\tside\n");
		}
		if (cmdf != nullptr)
		{
			fprintf(cmdf, "%d\t%02x\t%u\t%u\t%d\n",
				cmdn++,
				unsigned(data),
				unsigned(m_wd2797->track_r()),
				unsigned(m_wd2797->sector_r()),
				int(m_floppy->exists() ? m_floppy->ss_r() : -1));
			fflush(cmdf);
		}
	}
#endif
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
	uint8_t const v = (mem_mask & unixpc::LO_BYTE) ? uint8_t(data) : uint8_t(data >> 8);
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


harddisk_image_device *unixpc_state::hdc_image() const
{
	return m_hdc_xfer1 ? m_hdr1 : m_hdr0;
}

unsigned unixpc_state::hdc_head() const
{
	// HDSEL3 exists only on the P5.1 PAL. A P3...P5 board has 8 heads.
	unsigned const hi = (m_combo.p51() && m_hdc_head4) ? unixpc::HDSEL3_HEAD : 0u;
	return unsigned(m_hdc_head_low & unixpc::SDH_HEAD_MASK) | hi;
}

void unixpc_state::hdc_apply_head()
{
	m_hdc->head_w(hdc_head());
}


void unixpc_state::hdc_reg_w(offs_t offset, uint8_t data)
{
	unsigned const reg = offset & unixpc::TASK_REG_MASK;
	if (reg == 7)
	{
		// Sample CHS + HDSEL3 now. Later MCR2/SDH writes must not
		// retarget the in-flight 512-byte bursts (1K = LBA, LBA+1).
		hdc_apply_head();
		unsigned const head = hdc_head();
		m_hdc_next_lba = unixpc::winchester_lba(m_combo, m_hdc_cyl, head, m_hdc_sector & unixpc::SECTOR_NUM_MASK);
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
		// Mask width is the chip, not the board. WD2010 keeps bit 2
		// (11-bit cylinder). WD1010 drops it (10-bit, 1024 cylinders).
		// See DISK. DRUN is 74123/R63 glue, not a task-file bit.
		data &= m_combo.cyl_hi_mask();
		m_hdc_cyl = uint16_t((m_hdc_cyl & unixpc::LO_BYTE) | (uint16_t(data) << 8));
	}
	if (reg == 6)
	{
		m_hdc_head_low = uint8_t(data & unixpc::SDH_HEAD_MASK);
		hdc_apply_head();
		// The image is chosen by MCR2 D1, not by these bits.
		data = uint8_t((data & ~unixpc::SDH_DRIVE_MASK)
			| (m_hdc_drive1 ? unixpc::SDH_DRIVE1 : 0));
	}
	if (reg == 7)
	{
		m_hdc_xfer1 = m_hdc_drive1;
		unsigned const head = hdc_head();
		uint8_t const sec = uint8_t(m_hdc_sector & unixpc::SECTOR_NUM_MASK);
		harddisk_image_device *const img = hdc_image();
		if (img->exists())
			img->set_geometry(m_combo.cylinders(), m_combo.heads(), unixpc::WINCH_SPT);
		LOGMASKED(LOG_HDC, "hdc cmd %02x pc=%06x chs=%u:%u:%u nsec=%u lba=%u ptr=%06x cnt=%04x drv=%d\n",
			data, unsigned(m_maincpu->pc()), unsigned(m_hdc_cyl), head, unsigned(sec),
			unsigned(m_hdc_seccnt), unixpc::winchester_lba(m_combo, m_hdc_cyl, head, sec),
			m_dma.pointer(), m_dma.count(), int(m_hdc_xfer1));
		if (unixpc::winch_missing_second(m_hdc_xfer1, img->exists(), data))
		{
			m_hdc_next_lba = ~uint32_t(0);
			m_hdc->id_not_found();
			return;
		}
		// Tell the chip which image, after the head bits are already stored.
		uint8_t sdh = uint8_t(m_hdc_head_low & unixpc::SDH_HEAD_MASK);
		if (m_hdc_xfer1)
			sdh |= unixpc::SDH_DRIVE1;
		m_hdc->write(6, sdh);
	}
	// Sector number is 5 bits. Bit 5 is an HDSEL3 alias, not LBA.
	if (reg == 3)
		data &= unixpc::SECTOR_NUM_MASK;
	m_hdc->write(offset, data);
}


uint8_t unixpc_state::hdc_reg_r(offs_t offset)
{
	uint8_t v = m_hdc->read(offset);
	if ((offset & unixpc::TASK_REG_MASK) == 5)
		v = uint8_t((m_hdc_cyl >> 8) & m_combo.cyl_hi_mask());
	return v;
}


void unixpc_state::hdc_word_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	uint8_t const v = (mem_mask & unixpc::LO_BYTE) ? uint8_t(data) : uint8_t(data >> 8);
	hdc_reg_w(offset & unixpc::TASK_REG_MASK, v);
}


uint16_t unixpc_state::hdc_word_r(offs_t offset, uint16_t mem_mask)
{
	(void)mem_mask;
	uint8_t const v = hdc_reg_r(offset & unixpc::TASK_REG_MASK);
	return uint16_t(v) | (uint16_t(v) << 8);
}


void unixpc_state::hdc_bcr_w(int state)
{
	if (state)
		m_hdc_pos = 0;
}


void unixpc_state::hdc_out_data(uint8_t data)
{
	if (m_hdc_pos < unixpc::SECTOR_BYTES)
		m_hdc_buf[m_hdc_pos++] = data;
}


uint8_t unixpc_state::hdc_in_data()
{
	if (m_hdc_pos < unixpc::SECTOR_BYTES)
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
	harddisk_image_device *const img = hdc_image();
	if (img->exists())
		img->set_geometry(m_combo.cylinders(), m_combo.heads(), unixpc::WINCH_SPT);
	unsigned const head = hdc_head();
	uint8_t const sec = uint8_t(m_hdc_sector & unixpc::SECTOR_NUM_MASK);
	uint32_t lba = m_hdc_next_lba;
	if (lba == ~uint32_t(0))
		lba = unixpc::winchester_lba(m_combo, m_hdc_cyl, head, sec);

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
		if (img->exists() && lba != ~uint32_t(0) && lba < unixpc::winchester_sectors(m_combo))
			(void)img->read(lba, m_hdc_buf);
		else
			std::memset(m_hdc_buf, 0, sizeof(m_hdc_buf));
#if UNIXPC_HDC_TRACE
		hdc_log_burst();
#endif
	}
	m_hdc_next_lba = unixpc::winchester_lba_next(m_combo, lba);

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
	for (int bank = 0; bank < unixpc::KEY_BANKS; bank++)
		now[bank] = m_keys[bank].read_safe(0);

	// Two identical vblank samples before a report: host OS key-repeat
	// and 1-frame ioport glitches otherwise look like extra makes.
	bool same_as_seen = true;
	for (int bank = 0; bank < unixpc::KEY_BANKS; bank++)
		if (now[bank] != m_key_seen[bank])
			same_as_seen = false;
	if (!same_as_seen)
	{
		for (int bank = 0; bank < unixpc::KEY_BANKS; bank++)
			m_key_seen[bank] = now[bank];
		m_key_stable = 0;
	}
	else if (m_key_stable < 1)
		m_key_stable++;
	else
	{
		bool changed = false;
		for (int bank = 0; bank < unixpc::KEY_BANKS; bank++)
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
			for (int bank = 0; bank < unixpc::KEY_BANKS; bank++)
				m_key_down[bank] = now[bank];
			m_keys_held = unixpc::kbd_queue::any_down(now);
		}
	}

	s16 const sx = s16(m_mouse_x->read());
	s16 const sy = s16(m_mouse_y->read());
	uint8_t const mb = uint8_t(m_mouse_b->read() & unixpc::MOUSE_BUTTON_MASK);
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
		m_kbd_shift = uint16_t(b) << 1 | unixpc::KBD_STOP_BITS;
		m_kbd_bits = unixpc::KBD_FRAME_BITS;
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
	m_kbd_cell = (m_kbd_cell + 1) & unixpc::KBD_CELL_MASK;
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
	m_bpplus = (state != 0);
	LOG("bpplus %d emul=%d pc=%06x\n", state, int(m_parity_emul), unsigned(m_maincpu->pc()));
}


void unixpc_state::nmi_update()
{
	m_maincpu->set_input_line(M68K_IRQ_7, m_board.nmi_latched() ? ASSERT_LINE : CLEAR_LINE);
}


/***************************************************************************
    MISC
***************************************************************************/

uint16_t unixpc_state::gsr_r()
{
	// Bits 15 and 10 are the live parity-enable mirror. A fault snapshot
	// must not hold them: the board-revision probe turns the latch off
	// and requires the read to follow on the next instruction.
	uint16_t g = m_board.regs().genstat & ~unixpc::GSR_PIE_BITS;
	if (m_board.pie())
		g |= unixpc::GSR_PIE_BITS;
	// Live LPINT+ (bits 9 and 11; see GSR_LPINT). Not latched into the
	// fault snapshot. Diag Transfer Test's IRQ2 ISR reads bit 11.
	g = uint16_t((g & ~unixpc::GSR_LPINT) | (m_lp_int ? unixpc::GSR_LPINT : 0));
	static unsigned gsr_n = 0;
	if (m_lp_int && (++gsr_n % 10000) == 0)
		lp_trace("gsr-poll", g);
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
	// P5.1 only: MCR2 D4 is the PAL latch mirrored on TSR D4. The
	// probe sets the bit, reads $450001, then clears it and requires
	// the second read to be 0. A P3...P5 board has no mirror, so the
	// same probe reports P3...P5. D0 is HDSEL3. D1 selects the second
	// image on a P5.1 board (ICUS and Milton). D2 and D3 are the
	// third and fourth selects; UNIX has no software for them.
	// A full-word store replaces the latched bits.
	(void)offset;
	if (mem_mask & unixpc::LO_BYTE)
	{
		m_hdc_head4 = BIT(data, unixpc::MCR2_HDSEL3_BIT);
		if (m_combo.p51())
		{
			m_mcr2_p51 = BIT(data, unixpc::MCR2_P51_BIT);
			m_hdc_drive1 = unixpc::winch_use_second(true, BIT(data, unixpc::MCR2_DDRIVE1_BIT));
		}
	}
	else if (mem_mask & unixpc::HI_BYTE)
	{
		m_hdc_head4 = BIT(data, 8 + unixpc::MCR2_HDSEL3_BIT);
		if (m_combo.p51())
		{
			m_mcr2_p51 = BIT(data, 8 + unixpc::MCR2_P51_BIT);
			m_hdc_drive1 = unixpc::winch_use_second(true, BIT(data, 8 + unixpc::MCR2_DDRIVE1_BIT));
		}
	}
	hdc_apply_head();
	LOG("mcr2 %04x mask=%04x hdsel3=%d p51=%d\n", data, mem_mask, int(m_hdc_head4), int(m_mcr2_p51));
}


void unixpc_state::tcr_w(offs_t offset, uint16_t data)
{
	m_tcr->write_bit(offset >> unixpc::TCR_ADDR_SHIFT, BIT(data, unixpc::TCR_DATA_BIT));
}


uint16_t unixpc_state::tsr_r()
{
	// b3..b0 active-low: msg waiting, ring2, ring1, offhook. Idle = all 1s.
	// b4 mirrors MCR2 D4. The probe sets it, reads it, then clears it.
	// Phone bits on D0-D7; duplicate D4 into D12 so an even-byte
	// MOVE.B of $450000 still sees the PAL (D8-D15).
	uint16_t data = unixpc::TSR_IDLE;
	if (m_mcr2_p51)
		data |= unixpc::TSR_P51;
	return data;
}


uint16_t unixpc_state::rtc_r()
{
	return unixpc::rtc_read_word(m_rtc->data_r());
}


void unixpc_state::rtc_w(uint16_t data)
{
	// Board packing at $480000: D15=CE, D14=ALE, D13=WE, D11-D8=nibble.
	// Present the control pins first, then the data bus (see tc8250).
	m_rtc->we_w(BIT(data, unixpc::RTC_WE_BIT));
	m_rtc->ale_w(BIT(data, unixpc::RTC_ALE_BIT));
	m_rtc->ce_w(BIT(data, unixpc::RTC_CE_BIT));
	m_rtc->data_w(unixpc::rtc_control_nibble(data));
}


void unixpc_state::lp_trace(char const *what, unsigned extra)
{
	static FILE *tf;
	if (tf == nullptr)
	{
		tf = fopen("/tmp/lp.trace", "w");
		if (tf == nullptr)
			return;
		setvbuf(tf, nullptr, _IONBF, 0);
	}
	fprintf(tf, "%s pc=%06x extra=%08x busy=%d hold=%d sel=%d pap=%d flt=%d ack=%d irq=%d\n",
		what, unsigned(m_maincpu->pc()), extra,
		m_lp_busy, m_lp_hold_busy, m_lp_select, m_lp_paper, m_lp_fault,
		m_lp_ack, m_lp_int);
}

void unixpc_state::printer_busy_w(int state) { m_lp_busy = state != 0; }
void unixpc_state::printer_select_w(int state) { m_lp_select = state != 0; }
void unixpc_state::printer_paper_w(int state) { m_lp_paper = state != 0; }
void unixpc_state::printer_fault_w(int state) { m_lp_fault = state != 0; }

void unixpc_state::lp_apply_strobe()
{
	// Park device STROBE* idle only while busy is stretched. Gating on
	// LPINT+ as well deadlocked Transfer Test: it re-strobes before the
	// data write that clears LPINT, then waits forever for busy.
	if (m_lp_hold_busy)
	{
		m_centronics->write_strobe(1);
		if (m_lp_strobe == 0)
			m_lp_need_strobe_release = true;
		return;
	}
	if (m_lp_need_strobe_release)
	{
		if (m_lp_strobe != 0)
			m_lp_need_strobe_release = false;
		else
		{
			m_centronics->write_strobe(1);
			return;
		}
	}
	m_centronics->write_strobe(m_lp_strobe);
}

void unixpc_state::printer_strobe_w(int state)
{
	m_lp_strobe = state ? 1 : 0;
	lp_trace("strobe", unsigned(m_lp_strobe));
	lp_apply_strobe();
}

void unixpc_state::printer_ack_w(int state)
{
	bool const high = state != 0;
	// Leading ACK: stretch visible busy and park the device strobe idle.
	// LPINT+ is deferred until hold-end so Transfer Test cannot start the
	// next byte (and re-strobe) while the stretch is still parking STROBE*.
	if (unixpc::lp_ack_leading(m_lp_ack, high))
	{
		m_lp_hold_busy = true;
		lp_apply_strobe();
		m_lp_ack_timer->adjust(attotime::from_usec(unixpc::LP_ACK_DELAY_USEC));
		lp_trace("ack-lead", unixpc::LP_ACK_DELAY_USEC);
	}
	if (unixpc::lp_ack_trailing(m_lp_ack, high))
		lp_trace("ack-trail", 0);
	m_lp_ack = high;
}

TIMER_CALLBACK_MEMBER(unixpc_state::lp_ack_tick)
{
	m_lp_hold_busy = false;
	lp_apply_strobe();
	// Raise LPINT+ with not-busy (flip-flop + GSR bits 9/11 + IRQ2).
	m_lp_int = true;
	m_int02->in_w<2>(1);
	lp_trace("hold-end-irq", 0);
}

void unixpc_state::printer_int_clear()
{
	bool const was = m_lp_int;
	m_lp_int = false;
	m_int02->in_w<2>(0);
	if (was)
	{
		lp_trace("irq-off", 0);
		// LPINT+ no longer blocks strobe; apply any pending guest level.
		lp_apply_strobe();
	}
}

void unixpc_state::printer_data_w(offs_t offset, uint16_t data, uint16_t mem_mask)
{
	if (ACCESSING_BITS_0_7)
		m_printlatch->write(data);
	lp_trace("data", (unsigned(mem_mask) << 16) | data);
	printer_int_clear();
}

uint16_t unixpc_state::line_printer_r()
{
	uint16_t data = 0;

	data |= 1 << unixpc::LPSR_NO_DIALTONE;
	// Bit 1 clear means a parity error is latched (active until CSR).
	if (!m_board.perr())
		data |= 1 << unixpc::LPSR_NO_PERR;
	// Open connector floats high. A device overwrites the four lines.
	// Hold-busy stretches the pin so a ~20 us generic ACK still looks busy.
	data |= unixpc::lpsr_printer_bits(
		unixpc::lp_visible_busy(m_lp_busy, m_lp_hold_busy),
		m_lp_select, m_lp_paper, m_lp_fault);
	data |= m_hdc_intrq ? 1 << unixpc::LPSR_HDC_IRQ : 0;
	// ROM polls this bit, not the 68010 IRQ2 pin. Latch until FDC status is read.
	data |= (m_fdc_intrq || m_fdc_irq_latch) ? 1 << unixpc::LPSR_FDC_IRQ : 0;

	// Log on change, and periodically while LPINT is pending so a tight
	// poll loop (unchanging busy) still shows up in /tmp/lp.trace.
	static uint16_t seen = 0xffff;
	static unsigned poll_n = 0;
	bool const changed = ((data ^ seen) & unixpc::LPSR_PRINTER_FLOAT) != 0;
	if (changed)
		seen = data;
	if (changed || (m_lp_int && (++poll_n % 10000) == 0))
		lp_trace(changed ? "status" : "status-poll", data);

	return data;
}
