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
line.

The on-board **882A** lives at `$E60000`. Register select is A15..A12.
**RR2** (`$E62000`) is the presence/status byte: **`0xFF` means no modem**
(default, matching FreeBee). Machine Configuration **On-board modem stub**
or **`-modem`** / **`-nomodem`** (ini `modem`) makes RR2 report an idle
present modem (`CTS|DSR|1200` = `0x15`), latches WR0/WR1/WR4–WR6/WR8/WR9,
and clears LPSR “no dial tone” so Contiki can open `/dev/ph0` without the
“can not be opened” dialog. Dial/answer and Diagnostic Disk Modem tests
still have no phone line.

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

The CPU samples those odd addresses. The emulated expansion map is
16-bit, so the card handler sees the even word base and still returns
`0xff00 | byte` so D7–D0 (odd address) carry the identity or MAC octet.

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
| `+0` | LANCE data port (RDP). A **read** captures into the data latch (this cycle returns open bus); a **write** goes to the chip |
| `+2` | LANCE address port (RAP). Same latch-on-read as RDP. The driver writes RAP, waits, then uses RDP |
| `+4` | Control. Read clears the board interrupt. The driver writes `5` once |
| `+6` | Data latch on **read** (value captured by the previous RDP/RAP read). Status-ring address on **write** (byte address >> 9) |
| `+0x3FFCB` … `+0x3FFC1` | Station address, six odd bytes, octet 0 at the top |

Control bits: D0 DMA enable, D1 is 0 for Ethernet and 1 for the other
interface, D2 is 1 for interrupt level 1 and 0 for level 5, D3 spare.
The value `5` is DMA on, Ethernet selected, level 1. The emulated
station address is the local address `02:00:00:3b:00:01`.

### WIN/3B driver reverse notes (`ether` / `if_ether.c`)

Kernel `slotinit` (`/unix`, `0x7592`) walks eight windows from `$C00000`
through `$DC0000`. For each window it writes a word at `base+0x3FFF8`
(card reset), then reads four **odd** identity bytes into `slots[]`
(`0x25e3c`, eight records of four bytes: lo, hi, lō, hī). `getslot(id)`
(`0x75de`) matches `id` low/high against those bytes and the
complements summing to zero, then returns `0xC00000 + (index << 18)`.

The loadable driver `configure` table has one entry: `enprobe` /
`enattach` for unit 0. `enprobe` calls `getslot(0x1003)`. On success it
stores window-relative pointers (RDP/RAP/CTL/latch at `+0/+2/+4/+6`,
reset block at `+0x3FFF8`, MAC base at `+0x3FFCA`) and copies six MAC
octets from odd addresses `+0x3FFCB` downward by two to `+0x3FFC1`.
`enattach` builds an `ifnet` named `en` (so `en0`), MTU `0x5dc`
(1500), and hooks `eninit` / `enoutput` / `enioctl` / `enrelease` via
`if_attach`.

`/etc/.drvload` runs `./ether.rc` after a successful `lddrv -av ether`.
Stock `ether.rc` does `ifconfig en0 \`uname -n\``. If the node name is
still `UNIXPC` (not a usable address), ifconfig prints `UNIXPC: bad
value` and `.drvload` **unloads** the driver. This `ifconfig` also has
no `-a`; that flag is treated as an interface name and yields
`SIOCGIFFLAGS: no such interface`. Use `ifconfig en0` / `netstat -i`.

Working guest config for this tree: `/etc/hosts` entry
`192.168.9.50 mame3b1`, `ether.rc` using `IPADDR=192.168.9.50`, and
`mkdir -p /usr/spool/mqueue` so the sendmail line in `ether.rc` does
not fail the script.


### WIN/3B board datapath (RE)

Constants in the linked driver: **32** RX/TX descriptors (`nbufs=0x20`),
**128**-byte buffers (`bufsize=0x80`). `getlog` maps those to LANCE ring
length codes 5 and 7.

#### Register helpers

`setreg(csr, value)` writes RAP (`+2`) then RDP (`+0`) with two `nop`s
between. Writes go straight to the LANCE (no latch). CSR reads in this
driver do **not** use the latch path for the interrupt loop; they consume
a **board status ring** instead.

`enrelease` clears a word at `+0x3FFF8` (card reset) and deregisters the
IRQ. `enreset(unit)` is a thin wrapper that calls `eninit`.

#### `eninit` (bring LANCE online)

Requires an interface address already present (`softc+0x18+4` non-zero);
`ifconfig` therefore calls into ioctl/`ensetaddr` before init can finish.

1. `getlog(32)` / `getlog(128)` → ring-length fields for the init block.
2. Card reset: `clr.w` at `+0x3FFF8`.
3. `setreg(1, InitBlock & 0xffff)` then `setreg(2, InitBlock >> 16)` —
   CSR1/CSR2 = init-block address.
4. `setreg(3, 6)` — CSR3 = `ACON|BSWP` (byte swap for big-endian host).
5. Carve the `0x3000`-byte DMA arena from `enprobe`:
   - `[0, 0x400)` status-ring words, primed to `0xFFFF`
   - then 32 RX descriptors × 8 bytes
   - then 32 TX descriptors × 8 bytes
   - then packet buffers (128 bytes each)
6. Program the board status-ring base: store `(dma_base >> 9)` to `+6`
   (latch/status-ring port).
7. Build classic LANCE init block at `InitBlock` (`0x3795f4`): mode 0,
   PADR from the six MAC bytes (word-swapped pairs), LADRF from the
   logical-address filter, RDRA/TDRA with length codes in the high bits.
8. `NextCSR` ← status-ring base.
9. Control port `+4` ← `5` (DMA enable, Ethernet, IRQ level 1).
10. `setreg(0, 0x43)` — CSR0 = `INIT|STRT|INEA`.
11. Set `IFF_UP|IFF_RUNNING` (`0x41`), `if_rtinit`, `arpwhohas`.

Descriptor format used in the rings (8 bytes, LANCE RMD/TMD with BSWP):

| Off | RX | TX |
|---|---|---|
| +0 | buffer address low | buffer address low |
| +2 | addr high in low byte; `OWN=0x8000` when given to chip | same; STP/ENP bits set by `enstart` |
| +4 | `−bufsize` (BCNT) | BCNT (`−nbytes`) |
| +6 | MCNT (0 until chip fills) | TMD3 / error bits |

#### Transmit path

`enoutput` → ARP resolve (`arpresolve`) for AF_INET → prepend 14-byte
Ethernet header (dst/src/type) onto an `mbuf` chain → queue on
`softc` output list → `enstart`.

`enstart` dequeues one mbuf, calls `enput` to copy bytes into the next
owned TX buffer (may span multiple 128-byte descriptors), sets
`OWN|STP|ENP` (`0x8200` / per-seg `0x8000` + ENP on last), advances the
TX producer pointer (`0x3795dc`).

#### Interrupt / receive path

Board posts LANCE CSR0 snapshots into the status ring (empty = `0xFFFF`).
`enintr`:

1. If `*NextCSR == 0xFFFF`, no work.
2. Read control port (clears board IRQ).
3. Drain status words; high byte classifies the event:
   - `0x04xx` — receive → `enrint`
   - `0x02xx` — transmit complete / reclaim TX descriptors
   - `0x01xx` — ignore / continue
   - other — `"Bad CSR on interrupt"` and often `enreset`
4. Advance `NextCSR` by 2, wrap after `0x400` bytes; rewrite slot to
   `0xFFFF`.

`enrint` walks RX descriptors until `OWN` is clear, reads MCNT, calls
`enget` to build an mbuf chain from the buffer address, returns the
descriptor to the chip (`OWN|0x8000`), then demux: EtherTypes `0x800` /
`0x806` → IP input queue / `arpinput`. Missing ENP → `"RX_ENP not found"`.

#### `enioctl` / address

Recognizes `0x1020690c` (set address) and `0x10206910` (flags/up). Set
address copies the `ifreq` into softc via `ensetaddr` (also fills
`in_netof` / `in_lnaof` / broadcast), registers the IRQ once
(`0x377b1a` armed flag), then `eninit`. Down path calls `enrelease`.

#### Softc / globals (linked at `0x367000`)

| Addr | Role |
|---|---|
| `0x37960c`…`618` | RDP, RAP, CTL, status-ring port |
| `0x379620` | reset (`+0x3FFF8`) |
| `0x379628` / `62c` | MAC base / 6-byte copy |
| `0x3795f4` | InitBlock |
| `0x3795d0` / `5d4` | RX / TX descriptor rings |
| `0x3795d8` / `5dc` / `5e0` | RX consumer / TX producer / TX reclaim |
| `0x379644` / `648` | status-ring base / `NextCSR` |
| `0x37964c` | DMA arena pointer |
| `0x379650` | `en_softc` (stride `0x7e`) |

#### Emulator status-ring behaviour

On AM7990 `/INTR` assert (active low), the card copies CSR0 into the next
status-ring word with `INTR` and combinatorial `ERR` cleared, advances a
0x400-byte producer, clears the LANCE cause bits (so `/INTR` drops), and
raises the board IRQ. `enintr` drains that ring after reading the control
port and matches the status **high byte exactly** (`0x01` IDON, `0x02`
TINT, `0x04` RINT, `0x10` MISS, …). Combined high bytes such as `0x94`
(`ERR|MISS|RINT`) take `"Bad CSR on interrupt"` → `enreset` and wedge RX.

So the emulator: (1) does not set `RINT` on MISS in am7990; (2) strips
`ERR` and priority-encodes a single cause into the snap; (3) suppresses
MISS-only status posts (MISS does not run `enrint`, and a MISS storm
overwrites pending RINT/TINT words).

Verified live: boot `.drvload`/`ether.rc` → `eninit` raises IDON;
emulator logs `enet: status-ring post …`; guest drains the slot back to
`0xFFFF`; `en0` stays up; repeated telnet connect/login no longer wedges.

LANCE DMA must translate guest **logical** addresses through the MMU
(same as disk DMA). Treating them as physical made INIT read garbage and
left TX descriptors with `OWN` stuck forever. Clearing CSR0 cause bits
must also keep `INEA` set — a write with `INEA` clear disables further
board IRQs and drops the following TINT snapshot.

Lua dump: `/tmp/unixpc_ring_dump_request` → `/tmp/unixpc_status_ring.txt`.

### WIN/3B networking recipe (this tree)

Goal: boot UNIX 3.51 with Wollongong WIN/3B so guest `en0` is up as
`mame3b1` and peer ICMP/TCP work through host `pcap`.

Preferred Darwin path is a peered `feth` pair (avoids WiFi foreign-MAC
limits). LAN-on-`en0` still works for broadcast ARP; unicast needs wired
Ethernet or the feth recipe below.

#### Launch (feth / host↔guest, recommended on macOS WiFi)

```
# one-time (admin): create the pair and host address
sudo ifconfig feth0 create; sudo ifconfig feth1 create
sudo ifconfig feth0 peer feth1; sudo ifconfig feth1 peer feth0
sudo ifconfig feth0 up; sudo ifconfig feth1 up
sudo ifconfig feth0 inet 192.168.9.1 netmask 255.255.255.0
# guest ether.rc / hosts use IPADDR=192.168.9.50
# chmod bpf if needed: sudo chmod g+rw /dev/bpf*; sudo chgrp admin /dev/bpf*

# cfg/3b1.cfg: <device tag=":exp1:ethernet:lance" interface=N mac="02:00:00:3b:00:01"/>
# N = index of feth1 from ./unixpc -listnetwork

./unixpc 3b1 \
  -hard roms/3b1/3b1.hd \
  -rs232 pty \
  -modem \
  -exp1 ethernet \
  -networkprovider pcap \
  -autoboot_script roms/3b1/snapview.lua
```

Host checks after ether autoload (~2–3 min): `ping 192.168.9.50`,
`nc -vz 192.168.9.50 23` / `21`. Save: `sta/3b1/win3b.sta`.

Without `pcap` (provider `none`), TX still completes (LCAR then guest
reclaim) and `netstat` Opkts increments. Optional:
`-printer printer -prin roms/3b1/printout.prn`. Do **not** attach
`-rs232 loopback` for interactive Contiki/getty.

The log line `RS-232 pty /dev/ttysN` is the host PTY. Agents stash it in
`/tmp/unixpc_pty.path`. Wake getty with CR spam; log in as `root` with
an empty password, terminal type `s4`. Avoid guest `/dev/kmem` /
`/dev/mem` dumps — they can hang the shell; use the Lua triggers below.

#### Guest files already on `3b1.hd`

| Path | Role |
|---|---|
| `/etc/lddrv/ether.o` | WIN/3B driver (~111448 bytes on 3.51) |
| `/etc/lddrv/ether.rc` | `IPADDR=192.168.9.50` (feth) or LAN IP, `HOSTNAME=mame3b1`, daemons |
| `/etc/lddrv/drivers` | must list `ether` (with `lipc`) so `.drvload` autoloads |
| `/etc/hosts` | matching `mame3b1` IP plus localhost |
| `/usr/spool/mqueue` | directory present so `ether.rc` sendmail line is safe |
| `/usr/bin/telnet`, `/usr/bin/ftp`, `/usr/ethernet/…` | userland |

Stock `ether.rc` used `ifconfig en0 \`uname -n\``. With `uname -n` still
`UNIXPC` that fails and `.drvload` unloads ether — the guest copy above
uses a literal `IPADDR` instead.

#### Fresh install (only if the HD lacks WIN/3B)

Package: `roms/3b1/ethernet/ETHERNET+IN` (also under guest
`/usr/spool/uucppublic/ETHERNET+IN`). **Omit `cpio -B`** — that record
size truncates `new/etc/lddrv/ether.o` (111448 → 106496).

```
mkdir /tmp/installed && cd /tmp/installed
cpio -icduvm < /usr/spool/uucppublic/ETHERNET+IN
chmod +x Install Remove
# Contiki Office → UNIX window for ./Install (shform); or finish by hand:
cp new/etc/lddrv/ether.o /etc/lddrv/ether.o   # confirm size 111448
# edit ether.rc / hosts as above; echo ether >> /etc/lddrv/drivers
mkdir -p /usr/spool/mqueue
```

#### Verify after boot

```
/etc/lddrv/lddrv -sv          # ether ALLOC at ~0x367000
/usr/bin/ifconfig en0         # 192.168.9.50 flags=63<UP,…,RUNNING>
/usr/bin/netstat -in          # en0 + lo0 (no ifconfig -a on this ifconfig)
```

If ether is loaded but `en0` is missing, re-run
`/usr/bin/ifconfig en0 192.168.9.50 arp -trailers` (or `./ether.rc`).

Host-side Lua (snapview): touch `/tmp/unixpc_exp_dump_request` for slot
identity/MAC; `/tmp/unixpc_ring_dump_request` for softc + status ring →
`/tmp/unixpc_status_ring.txt`; `/tmp/unixpc_type_request` for natkeyboard
post. With `-verbose`, IDON posts log as
`enet: status-ring post csr0=01c3 snap=0143 @…`.

Wire MAC is `02:00:00:3b:00:01` (init-block `put_u16le` of PROM
`01:00:3b:00:00:02`). Put that MAC in `cfg/3b1.cfg` so pcap installs a
matching BPF filter before open.

Verified (Darwin feth): `ping 192.168.9.50` 0% loss; TCP 23/21 open;
telnet banner `UNIX System V (TWG-TCP/IP) (mame3b1)`; FTP
`220 mame3b1 FTP server (Version 1.2 Jul 07 1986)`. Save:
`sta/3b1/win3b.sta`.

#### Known gaps / Darwin notes

- Host `netdev` defaults to `none` unless `-networkprovider pcap`. BPF
  `/dev/bpf*` is root-only; `chmod g+rw /dev/bpf*` (admin) or sudo /
  ChmodBPF. Bind LANCE via `cfg/3b1.cfg` `<network>` `interface=` from
  `-listnetwork`.
- macOS WiFi `en0` does not deliver foreign-MAC unicast into BPF. Use the
  feth recipe (above), a wired NIC, or Thunderbolt Ethernet for ICMP/TCP.
- am7990 RX pads to 60 and appends FCS for tap/pcap frames (host stacks
  omit FCS; without that the C-LANCE CRC error bit drops every packet).
- Contiki graphics login is separate from the serial PTY getty; Contiki
  screen redraws can flood the PTY — prefer short marker commands or
  `/tmp/unixpc_type_request`.

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
- Phone line. Channel B has only its baud clock. `-modem` is a presence
  stub for Contiki `/dev/ph0` open; not a real 882A / telephony path.
- Third and fourth hard disks.
- StarLAN, DOS-73, FloppyTape, QIC-02, the expansion chassis, Voice Power,
  and the PBX card.
- Full DRAM / DMA parity (see RAM parity above).
