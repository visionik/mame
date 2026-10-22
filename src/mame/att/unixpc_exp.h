// license:GPL-2.0+
// copyright-holders:Dirk Best, R. Belmont, Visionik

#ifndef MAME_ATT_UNIXPC_EXP_H
#define MAME_ATT_UNIXPC_EXP_H

#pragma once

#include "machine/am79c90.h"
#include "machine/z80scc.h"

#include "unixpc_enet.h"
#include "unixpc_slot.h"

// S4BUS slot. An empty window finishes the cycle and reads 0xFF, so the
// identity complements fail and the slot is skipped. Three chassis slots.
// A 4 MB machine is 2 MB on the board plus one card.
//
//   eia       Dual EIA. Z8530 at the combo-driver offsets, identity
//             0x0010. Clock is the built-in 7201 clock, 19.6608 MHz / 8.
//             The SCC interrupt is autovector 5, shared by every chassis
//             slot. Ethernet joins that line when its select bit is clear.
//   ethernet  AT&T LANCE card, identity 0x1003, interrupt level 1.
//   ram       RAM only: 512 KB or 2 MB. Not a slot option yet.
//   eia_ram   EIA/RAM combo. Same UART map; the RAM size is not wired.
//
// StarLAN, DOS-73, tape, the expansion chassis, and the voice/PBX cards
// are not planned.

class unixpc_card_device : public device_t
{
public:
	virtual uint16_t exp_r(uint32_t offset) = 0;
	virtual void exp_w(uint32_t offset, uint16_t data, uint16_t mem_mask) = 0;

protected:
	using device_t::device_t;
};

class unixpc_eia_device : public unixpc_card_device
{
public:
	unixpc_eia_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual uint16_t exp_r(uint32_t offset) override;
	virtual void exp_w(uint32_t offset, uint16_t data, uint16_t mem_mask) override;

protected:
	virtual void device_start() override ATTR_COLD { }
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

private:
	void irq_w(int state);
	uint8_t low_byte(uint16_t data, uint16_t mem_mask) const;

	required_device<scc8530_device> m_scc;
	required_device<input_merger_device> m_irq;
};

class unixpc_enet_device : public unixpc_card_device
{
public:
	unixpc_enet_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	virtual uint16_t exp_r(uint32_t offset) override;
	virtual void exp_w(uint32_t offset, uint16_t data, uint16_t mem_mask) override;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

private:
	void irq_w(int state);
	void publish_irq();
	// Returns true if a status-ring word was posted (and board IRQ should rise).
	bool post_status_csr0();
	// If guest NextCSR sees 0xFFFF but our producer drifted, resync and
	// optionally plant a RINT so enrint drains completed RX descriptors.
	bool sync_status_ring_prod(bool plant_rint);
	u16 dma_r(offs_t offset);
	void dma_w(offs_t offset, u16 data, u16 mem_mask = ~0);

	required_device<am7990_device> m_lance;
	required_device<input_merger_device> m_irq;
	required_device<input_merger_device> m_irq5;
	required_device<m68010_device> m_cpu;
	unixpc::enet_board m_board;
	uint8_t m_mac[6];
	bool m_lance_level = false; // true while AM7990 /INTR is asserted (active low)
};

class unixpc_exp_device : public device_t, public device_slot_interface
{
public:
	unixpc_exp_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	unixpc_exp_device(const machine_config &mconfig, const char *tag, device_t *owner,
		void (*opts)(device_slot_interface &), const char *dflt, unsigned index = 0);

	unsigned slot_index() const { return m_slot; }

protected:
	virtual void device_start() override ATTR_COLD { }

private:
	unsigned m_slot = 0;
};

DECLARE_DEVICE_TYPE(UNIXPC_EIA, unixpc_eia_device)
DECLARE_DEVICE_TYPE(UNIXPC_ENET, unixpc_enet_device)
DECLARE_DEVICE_TYPE(UNIXPC_EXP, unixpc_exp_device)

void unixpc_exp_cards(device_slot_interface &device);

#endif
