# UNIX PC / 3B1 hardware map

Notes for the MAME `unixpc` driver. Addresses are 68010 physical.
A 16-bit access uses the low data byte unless a register is described
as a full word. An empty bus reads `0xFF`.

Sources: the 1986 UNIX PC reference manual, the Ethernet expansion
board theory of operation, the EIA/RAM combo expansion disk (3.51),
and the WIN/3B install archive `ETHERNET+IN` (loadable driver
`etc/lddrv/ether.o`).

## Clocks

| Clock | Frequency | Used by |
|---|---|---|
| System | 40 MHz | Divided by 4 for the 68010 (10 MHz), by 2 for the dot clock (20 MHz) |
| 7201 / Z8530 | 19.6608 MHz / 8 = 2.4576 MHz | Built-in serial chip and the combo-card Z8530 |
| RS-232 baud generator | 1.2288 MHz | External RxC/TxC for the built-in port only |
| Modem channel | 19.2 kHz | 7201 channel B when MCR bit 12 is set |
| RTC | 32.768 kHz | TC8250 |

With the Z8530 baud-rate generator in 16× mode, bit rate is
`76800 / (time constant + 2)`. Exact rates: 38400, 19200, 9600, 4800,
2400, 1200, 600, 300, 150, 75. The built-in port's generator is
`1.2288 MHz / (4*N)` into an x16 clock: N=1 is 19200, N=2 is 9600,
N=16 is 1200, N=64 is 300. N=0 stops that clock.

## Interrupt levels

All of these are autovectored. The board answers interrupt acknowledge
with a valid-peripheral-address handshake, so the 68010 builds the
vector itself. Level *n* is vector `24+n`, at byte offset `4*(24+n)`.

| Level | Vector | Offset | Source |
|---|---|---|---|
| 1 | 25 | `0x64` | Expansion. Ethernet selects this when control bit 2 is set |
| 2 | 26 | `0x68` | Line printer, floppy, and hard disk, on one shared request |
| 3 | 27 | `0x6c` | Keyboard and modem |
| 4 | 28 | `0x70` | Built-in RS-232 (7201) |
| 5 | 29 | `0x74` | Expansion. Dual EIA is fixed here. Ethernet selects this when control bit 2 is clear |
| 6 | 30 | `0x78` | 60 Hz clock |
| 7 | 31 | `0x7c` | Parity and memory-management faults |

The combo driver registers its service routine on level 5. The Z8530
interrupt pin is tied to that autovector. Expansion cards use level 1
or level 5, and the three chassis slots share each of those lines.

The printer request is a flip-flop, `LPINT+`, not the acknowledge pin
itself. Centronics pin 10 (`LPACK*`) is active-low. The trailing edge
of that pulse, the return to high, clocks the flip-flop. The priority
encoder turns `LPINT+` into level 2 when no higher request is pending.
Software clears the request by writing the line-printer data register.
The boot ROM does that by writing `0000` to `$4F0000`. The acknowledge
cycle does not clear it. A read of `$470000` does not clear it either.

Floppy and hard-disk interrupts are also visible as bits 3 and 2 of
`$470000`. The boot ROM polls the floppy bit there. It does not rely
on the level-2 pin alone.

## On-board I/O

Zone `$400000` repeats every 64 KB. The boot ROM is the 16 KB at
`$800000` only (two 8 KB chips). User-mode accesses at or above
`$400000` take a privilege bus error (GSR `0x10ff` class) when Error
Enable is on; supervisor and DMA may use the window. With VIDPAL set,
user may also use video RAM `$420000`–`$427FFF`.

| Address | Access | Function |
|---|---|---|
| `$000000`–`$1FFFFF` | RAM | Motherboard RAM, at most 2 MB |
| `$200000`–`$3FFFFF` | RAM | Expansion RAM |
| `$400000` | R/W | Map RAM, 1024 page entries |
| `$410000` | R | General status. Bits 15 and 10 are the live parity-enable mirror; `LPINT+` is bits 9 and 11 (`0x0A00` — Diagnostic Disk ISR tests bit 11) |
| `$420000` | R/W | Video RAM, 32 KB |
| `$430000` | R | Bus status 0 |
| `$440000` | R | Bus status 1 |
| `$450000` | R | Telephone / board status |
| `$460000` | R/W | Disk DMA count |
| `$470000` | R | Line-printer, modem, and disk-interrupt status |
| `$480000` | W | Real-time clock control |
| `$490000` | W | Timer control |
| `$4A0000` | W | Miscellaneous control (strobe, DMA direction, vertical blanking) |
| `$4B0000` | W | Dialer page, 4 KB. Baud divisor when A10 and A11 are clear |
| `$4C0000` | R/W | Miscellaneous status / control |
| `$4D0000` | R/W | Disk DMA pointer |
| `$4E0001` | W | Disk control |
| `$4F0001` | W | Printer data latch |
| `$800000` | R | Boot ROM, 16 KB |
| `$C00000` | R/W | Expansion I/O, eight 256 KB windows |
| `$E00000` | R/W | WD1010 task file |
| `$E10000` | R/W | WD2797 floppy |
| `$E20000`, `$F20000` | W | Second miscellaneous control (MCR2) |
| `$E30000` | R | Real-time clock data |
| `$E40000` | W | General control (parity enable and related latches) |
| `$E50000` | R/W | UPD7201, built-in serial and modem |
| `$E60000` | R | Modem. Second register at `+$2000` |
| `$E70000` | R/W | Keyboard controller (6850) |

### `$470000` status

| Bit | Signal | A 1 means |
|---|---|---|
| 7 | Busy | Printer cannot take a byte. Also set for paper-out or fault |
| 6 | Select | Printer is selected |
| 5 | Paper | Out of paper |
| 4 | Fault | No error. A 0 means error. The connector pin is low for paper-end, offline, or a fault |
| 3 | Floppy interrupt | Interrupt pending |
| 2 | Hard-disk interrupt | Interrupt pending |
| 1 | Parity | No parity error |
| 0 | Dial tone | No dial tone |

Acknowledge is not in this word. With nothing attached, bits 4–7 float
high: busy, selected, out of paper, and no fault.

Printer data is the latch at `$4F0001`. The strobe is miscellaneous
control bit 5, inverted onto the connector. The Centronics slot stays
empty by default. For Diagnostic Disk Parallel Printer (menu 10) and
to capture bytes, plug MAME’s generic printer and mount a printout:

```text
./unixpc 3b1 -flop1 roms/3b1/floppies/01_Diagnosic_Disk_Ver_3.51.IMD.work \
  -hard roms/3b1/3b1.hd -printer printer -prin roms/3b1/printout.prn \
  -autoboot_script roms/3b1/snapview.lua
```

`-prin` creates/loads a `.prn` file so the device is online (paper clear).
Printed bytes append to that file. The generic printer’s ACK finishes in
about 20 µs. For that window the board stretches visible busy, parks
cable `STROBE*` idle toward the device (avoids a second ACK if the guest
still holds strobe active), and raises `LPINT+` when busy is released so
not-busy and the interrupt arrive together. Diagnostic Disk Transfer Test
waits on that IRQ (GSR bit 11 / `LPFint` flag) before the next byte;
raising `LPINT+` early let it re-strobe into the park window and hang.
`LPINT+` stays latched until a printer-data write clears it.

### Save states

`3b1` declares `MACHINE_SUPPORTS_SAVE`. Driver state includes board/DMA/kbd,
disk combo, printer handshake, and the BPPLUS poison set (via a sorted
vector snapshot). Timers and device RAM are handled by the core.

Interactive: **F6** / **Shift+F6** save, **F7** / **Shift+F7** load
(files under `sta/3b1/`). CLI: `-state <name>` loads at start; `-autosave`
saves on exit.

Automated smokes (headless):

```text
./unixpc 3b1 -flop1 …IMD.work -hard …/3b1.hd \
  -video none -sound none -nothrottle \
  -autoboot_script roms/3b1/savestate_smoke.lua

./unixpc 3b1 … -autoboot_script roms/3b1/savestate_quit.lua
./unixpc 3b1 … -state smoke -autoboot_script roms/3b1/savestate_resume.lua
```

### MCR2 (`$E20000` and `$F20000`)

| Bit | Name | Behavior |
|---|---|---|
| 0 | HDSEL3 | Fourth head select. Only a P5.1 board uses it (heads 8–15) |
| 1 | Second drive | Selects the second hard-disk image. Only on P5.1 |
| 2 | Third drive | Decoded on Milton's board. No UNIX software |
| 3 | Fourth drive | Decoded on Milton's board. No UNIX software |
| 4 | P5.1 | Latched and mirrored to telephone-status bits 4 and 12. The probe must see it set, then clear |

A P3…P5 board has no bit-4 mirror, so the same probe reports P3…P5.

### Real-time clock

Toshiba TC8250 (`machine/tc8250`), 32.768 kHz. The chip exposes pin
writers (`ce_w`, `ale_w`, `we_w`, `data_w` / `data_r`). The write at
`$480000` is board packing: D15 chip enable, D14 address latch, D13
write enable, D11–D8 the nibble. The driver presents those pins, then
the nibble. Address loads while ALE is set; CE+WE with ALE clear
writes the nibble. CE alone is the read strobe and must not store
(or a 0 nibble would wipe each digit as the kernel reads the clock).
A read at `$E30000` returns the selected digit on D0–D3 and the same
nibble on D8–D11. Registers 0–C are BCD time digits; D–F are Tout, protect
key (write `5` to unlock), and status. Status write is second-zeroize
via dirtc `adjust_seconds()` (secs ≥30 carry into minutes); nibble RAM
must be committed into dirtc first so a double bus strobe cannot advance
minutes twice. Ten-month digit (reg 9): D0 = month tens, D3/D2 = L1/L0
leap select; on read D1 is leap-recognized and is ignored on write.
Writes to time digits stick. UNIX 3.51 treats years 00–86 as 2000–2086,
so `machine_reset` pins the year at 87 after the battery-backed sync.
Diagnostic Disk “Real Time Clock” (menu 11) passes with this model.
`tc8250_device::control_w` is only a convenience for the same packing.

### Built-in serial and modem

The UPD7201 is at `$E50000`, low byte, control and data for two
channels. Channel A is the RS-232 port (`-rs232`, guest `tty000`).
Channel B is the modem. Its baud clock is the fixed 19.2 kHz only
when MCR bit 12 is set (x16 is 1200, x64 is 300). There is no phone
line. A read of the modem's second register (`$E62000`) is `0xFF`
when no modem is installed.

The slot stays empty by default. With nothing plugged in, CTS and DCD
are held ready so UNIX `tty000` still transmits. For the Diagnostic
Disk Communications tests (menu 6), plug MAME’s loopback:

```text
./unixpc 3b1 -flop1 roms/3b1/floppies/01_Diagnosic_Disk_Ver_3.51.IMD.work \
  -rs232 loopback -autoboot_script roms/3b1/snapview.lua
```

`loopback` ties TXD→RXD, RTS→CTS, DTR→DSR+DCD (DSR enters the 7201 on
SYNCA). If the diag reports `DCD or RTS not working`, try
`-rs232 dec_loopback` instead (RTS→CTS+DCD, DTR→DSR+RI). Host access
under UNIX uses `-rs232 pty` or `-rs232 null_modem`.

## Expansion slots

Eight windows of 256 KB. The chassis wires the first three.

| Window | Base | Slot option |
|---|---|---|
| 0 | `$C00000` | `-exp0` |
| 1 | `$C40000` | `-exp1` |
| 2 | `$C80000` | `-exp2` |
| 3 | `$CC0000` | not wired on this chassis |
| 4 | `$D00000` | |
| 5 | `$D40000` | |
| 6 | `$D80000` | |
| 7 | `$DC0000` | |

An empty window completes the cycle and reads `0xFF`. The identity
check fails, and the slot is skipped. Unmapped expansion I/O must not
bus-error: the kernel walk does not catch one.

### Card identity

Four low bytes, on the odd addresses at the top of the window. Each
byte plus its complement is 0.

| Offset | Byte |
|---|---|
| `+0x3FFF9` | Low identity |
| `+0x3FFFB` | High identity |
| `+0x3FFFD` | Two's complement of the low byte |
| `+0x3FFFF` | Two's complement of the high byte |

A write anywhere in `+0x3FFF8` through `+0x3FFFF` resets the card.

| Card | 16-bit id | Low, high, low complement, high complement |
|---|---|---|
| EIA / EIA-RAM combo | `0x0010` | `10 00 F0 00` |
| Ethernet | `0x1003` | `03 10 FD F0` |
| Floppy-tape example from the bus spec | `0x0110` | `10 01 F0 FF` |

The floppy-tape id is the published example. That card is not emulated.
The combo driver does not load the 16-bit id itself. It keeps a slot
whose kernel record is `00 10 00 F0` (high, low, high complement, low
complement). The Ethernet driver asks the kernel for `0x1003` and does
not read the identity bytes. There is no separate identity for an
EIA-only card. The combo disk is the one that was read.

### Dual EIA (Z8530)

`-exp0 eia`. Channel A is `-exp0:eia:port1`. Channel B is
`-exp0:eia:port2`. Offsets are bytes from the window base. The low
byte of each word is the register. Per-port Diagnostic Disk self-test
(`Checking RTS,DTR,CTS,DCD,DSR`) needs a loopback on each port:

```text
./unixpc 3b1 -flop1 …IMD.work -exp0 eia \
  -exp0:eia:port1 loopback -exp0:eia:port2 loopback
```

DSR is wired into the SCC SYNC pins. Cross-port
`Transmit from port X. Receive from port Y` needs a null-modem between
the two ports and is not covered by per-port loopback alone.

| | Control | Data |
|---|---|---|
| Channel B | `+0` | `+4` |
| Channel A | `+2` | `+6` |

Writing a register number to a control word selects that register.
The next access of the same word is the register. A single store to
the control word is an immediate command. The service routine reads
channel A's control (register 3, low 6 bits) and channel B's control
(register 2, bits 1–3) to see which channel is asking. Clock is
2.4576 MHz. The interrupt output is 68010 level 5. There is no select
bit. A card reset drops the request with the chip.

### Ethernet (AMD LANCE)

`-exp1 ethernet`. Offsets from the window base.

| Offset | Register |
|---|---|
| `+0` | LANCE data port |
| `+2` | LANCE address port. The driver writes it, waits two instructions, then uses the data port |
| `+4` | Control. Read clears the board interrupt. The driver writes `5` once |
| `+6` | Status-ring address on write. The stored word is the byte address shifted right by 9. The data latch shares this address. The driver never reads it |
| `+0x3FFCB` … `+0x3FFC1` | Station address, six odd bytes, octet 0 at the top |

Control bits: D0 DMA enable, D1 is 0 for Ethernet and 1 for the other
interface, D2 is 1 for interrupt level 1 and 0 for level 5, D3 spare.
The value `5` is DMA on, Ethernet selected, level 1. The emulated
station address is the local address `02:00:00:3b:00:01`.

### Expansion RAM

Motherboard RAM is the low 2 MB. Reads inside that region wrap in the
installed motherboard size. Writes past that size, still below 2 MB,
are dropped. Expansion RAM is decoded from `$200000` for the installed
length. No identity check is required for the decode. Past the end,
through `$3FFFFF`, reads are `0xFF` and writes are ignored.

| Card | Where it sits |
|---|---|
| 512 KB | One half-megabyte bank chosen by the slot id: `$200000 + id*0x80000` |
| 1 MB | Slot ids 0 and 1 share `$200000`–`$300000`. Ids 2 and 3 share `$300000`–`$400000`. Two cards with the same pair conflict |
| 1.5 MB EIA/RAM | `$200000`–`$380000`, slot id ignored |
| 2 MB | `$200000`–`$400000`, slot id ignored |

A 4 MB machine is 2 MB on the board plus the 2 MB card. The current
4 MB option is that split, at the same addresses as one 4 MB region.

## Hard disk

The motherboard revision and the Winchester chip are independent.
Factory boards left with a socketed WD1010. A WD2010 in that socket is
an owner upgrade. P5.1 is the field PAL (head select and the bit-4
loopback), not a factory model.

| Board | Chip | Heads | Cylinders | Cylinder-high mask |
|---|---|---|---|---|
| P3…P5 | WD1010 | 8 | 1024 | `0x03` (bit 2 does not stick) |
| P3…P5 | WD2010 | 8 | 1400 | `0x07` (bit 2 sticks) |
| P5.1 | WD1010 | 16 | 1024 | `0x03` |
| P5.1 | WD2010 | 16 | 1400 | `0x07` |

The boot image is 1400/16/17, the last row. Sectors per track are 17.
The device object is MAME's WD1010. The cylinder-high mask is applied
in the driver. Machine Configuration selects the other three rows.
The default is P5.1 and WD2010.

The second drive is MCR2 bit 1 on a P5.1 board, mounted with `-hard2`.
The first image stays `-hard`. With no second image, seek, read, write,
and format return ID-not-found and raise the disk interrupt. Recalibrate
still finishes. UNIX nodes are `/dev/fp010` through `/dev/fp01f`.
ICUS (Tropiano and Kloepfer) and John Milton's board both decode that
one bit. They are not separate machine configurations.

The raw image layout, volume home block, spare sector, and s5fs
superblock are in `unixpc_image.md`. What `filsys` has to change to
mount one is in `unixpc_filsys.md`.

## Floppy

The controller is the WD2797 at `$E10000`. Boot order is screen, cursor,
banner, then floppy, then hard disk. With no image, status stays Not
Ready (`0x80`) after reset, so the ROM skips the floppy and opens the
hard disk. A mounted image uses the WD2797 (`FD_SEL` attaches the
drive; rising MR* starts restore). Disk DMA is 16-bit; each WD2797 DRQ
moves one byte, so a write word is split across two DRQs (high then
low). Feeding both bytes on one DRQ overwrote the format stream and
made post-format Write Sector return RNF on the VHB. The high-byte DRQ
fetches the DMA word and can retire the count while the low byte is
still pending — that final DRQ must still `data_w` the pending byte
(surface test: last word `FFFFF7F7` vs `FFFFF780` was a repeated `F7`).

## RAM parity (diagnostic)

GCR LS259: q0 = Error Enable (EE), q1 = Parity Interrupt Enable (PIE),
q2 = BP/BPPLUS (force bad parity on the next RAM write), q3 = ROMLMAP.
PIE is mirrored live in GSR bits 15 and 10 (`0x8400`). A latched parity
error clears LPSR bit 1 (NO_PERR) and asserts level-7 until Clear Status
(`CSR`). PIE gates the parity NMI; EE is not required for this path (the
Diagnostic Disk enables only PIE around the probe read). EE still gates
MMU bus errors and DMA NMI.

The driver does not store real DRAM parity bits. Machine Configuration
**RAM parity emulation** (off by default; takes effect on reset) enables
a sparse poison model for the Diagnostic Disk Memory & Parity subtest:
with BPPLUS set, mapped RAM writes mark that physical word; with PIE set
and the word still poisoned, a mapped read freezes GSR/BSR, clears
NO_PERR, and raises level-7. BSR uses the real byte address (odd-byte
cycles set the low bit — the diag probes `$23001` and checks BSR0/BSR1
against that VA). A normal write clears poison for that word. Lookups
run only when the option is on, PIE is set, and the poison set is
non-empty. Command line **`-parity`** / **`-noparity`** (or `parity` in
an ini) override the Machine Configuration when set. DMA parity and
continuous parity generation are not modeled.

## What is not wired

- EIA/RAM combo RAM size. The UART map is the same as the EIA-only card.
- A RAM-only slot card. The 4 MB option already occupies `$200000`–`$3FFFFF`.
- Phone line. Channel B has only its baud clock.
- Third and fourth hard disks.
- StarLAN, DOS-73, FloppyTape, QIC-02, the expansion chassis, Voice Power,
  and the PBX card.
- Full DRAM / DMA parity (see RAM parity above).
