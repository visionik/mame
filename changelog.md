# AT&T 3B1 / UNIX PC (`att-3b1` branch)

Changelog for the [visionik/mame](https://github.com/visionik/mame) `att-3b1` work on top of upstream [mamedev/mame](https://github.com/mamedev/mame). Driver lives under `src/mame/att/`; hardware notes in `src/mame/att/unixpc_hw.md`.

## Unreleased

_(nothing yet)_

## 2026-10-20 — WIN/3B ethernet TX/RX (`e9c21f29`)

Peer ICMP/TCP (ping, telnet, ftp) works on Darwin via `feth0`/`feth1` + `-networkprovider pcap`.

- **Status-ring CSR0 posting** for WIN/3B `enintr`: strip `INTR` and combinatorial `ERR`, priority-encode a single high-byte cause, clear LANCE causes while keeping `INEA`.
- **LANCE DMA** through the MMU (logical → physical); board reset holds the chip in `STOP`.
- **am7990 RX**: pad to 60 bytes and append FCS so host pcap frames (no FCS) pass the residue check; MISS raises `MISS|ERR|INTR` only (no `RINT`).
- **Telnet RX-ring wedge fix**: suppress MISS-only status posts; `sync_status_ring_prod` resyncs the producer to guest `NextCSR` and can plant a `RINT` so `enrint` reclaims descriptors.
- **Darwin pcap**: 2048-deep RX ring; BPF filter `not ether src <mac> and (broadcast|multicast|ether dst <mac>)`; apply cfg MAC before `set_interface`; no `PCAP_D_IN`.
- Recipe: guest `192.168.9.50` / host `feth0` `192.168.9.1`, MAC `02:00:00:3b:00:01`, `-exp1 ethernet`. Prefer `/usr/bin/nc` or `/usr/bin/python3` when Homebrew hits Local Network privacy.

## 2026-10 — On-board 882A modem stub (`6e6e849b`)

- Default RR2 `$E62000` = `0xFF` (modem absent, FreeBee-compatible).
- `-modem` / Machine Config “On-board modem stub”: idle present `CTS|DSR|1200`, latches WR0/1/4–6/8/9, clears LPSR no-dial-tone so Contiki can open `/dev/ph0`.
- No phone line or Diagnostic Modem path yet.

## Earlier — Diagnostic Disk, RTC, save states (`dacd1301`)

Diagnostic Disk Ver 3.51 green path for floppy Format/WriteSector/surface, Memory & Parity (`-parity`), Processor (with `-hard`), Parallel Printer, and RTC menu 11.

- Real **Toshiba TC8250** RTC (leap L1/L0, status second-zeroize).
- Floppy: 16-bit DMA → byte-DRQ split + pending-low drain.
- Centronics: `GSR_LPINT` bits 9+11; raise `LPINT` at busy hold-end.
- `MACHINE_SUPPORTS_SAVE`; driver split into `unixpc_{m,v,a}`; docs in `unixpc_hw.md`.
- Interactive UNIX: do **not** attach `-rs232 loopback` (getty IRQ storm).

## Earlier — Display, mouse, UI chords (`7af8e357`, `d1adc799`)

- Live MCR2 D4 / TSR for P5.1 loopback probe.
- Window scales 1.5×–3× at 720×348; mouse acceleration slider; 838A dialer two-tone.
- `Cmd+Shift+U` toggles MAME UI; `Cmd+Shift+M` frees the mouse without pause.
- Coalesce mouse DMA to one 6850 packet at a time.

## Earlier — 68010 BERR, keyboard, mouse, phosphor (`aeec3a18`)

- Format-8 bus errors; honour `RR=1` on RTE; Winchester LBA latch including HDSEL3.
- Keyboard: stable ioport edge (one make, then `0x40`); mouse `0xCE` packets.
- Phosphor white/green/amber; Catch tests for SSW, kbd, mouse.

## Earlier — Boot UNIX 3.51 (`5438eff2`)

- Custom 4K mapper, 68010 format-8 BERR, disk DMA, WD2010-class HDC (label geometry), RTC, 6850 keyboard.
- Interleaved Winchester DMA; force raw-image CHS; clamp writes to file.
- Boots AT&T 3B1 UNIX 3.51 to root / Contiki install logins.

## Launch (networked Contiki)

```bash
./unixpc 3b1 -hard roms/3b1/3b1.hd -rs232 pty -modem -exp1 ethernet \
  -networkprovider pcap -autoboot_script roms/3b1/snapview.lua
```

Guest: hostname `mame3b1`, IP `192.168.9.50`. Host: `feth0` at `192.168.9.1`, peer `feth1` bound in `cfg/3b1.cfg`. Telnet: `/usr/bin/nc 192.168.9.50 23` → `root` / empty password / terminal `s4`.
