# UNIX PC hard-disk image

A 3B1 hard-disk image is a raw sector array. Sector `N` is 512 bytes at
file offset `N * 512`. There is no container header. The boot image at
`roms/3b1/3b1.hd` is 194969600 bytes, which is 1400 × 16 × 17 × 512.
A 67 MB image of the same family is 71303168 bytes, 1024 × 8 × 17 × 512.
Geometry comes from the label in sector 0, not from the file size.

The controller addresses 17 sectors per track. The UNIX kernel does not.
It uses 16 data sectors and keeps the last sector of each track as a
spare for bad-block replacement. A logical block is 1024 bytes, two
data sectors, and a track holds 8 of them. This is a software convention
in the disk driver. The WD1010 itself is content with 17 sectors.

## Volume home block

Sector 0 is the volume home block. The on-disk layout is `struct vhbd`
from the UNIX PC `<sys/gdisk.h>`. Thirty-two-bit fields are 4 bytes wide
and aligned to 2. The 32-bit sum of the 512 bytes is `0xffffffff`. The
checksum word is the adjustment that makes that sum.

| Offset | Field | `3b1.hd` |
|---|---|---|
| 0 | Magic `UQVQ` | `0x55515651` |
| 4 | Checksum | `0x05e94dca` |
| 8 | Name, 6 bytes | `WINCHE` |
| 14 | Cylinders | 1400 |
| 16 | Heads | 16 |
| 18 | Physical sectors per track | 17 |
| 20 | Physical sectors per cylinder | 272 = 16 × 17 |
| 22 | Flags | `0x08`, the new partition table |
| 23 | Step rate | 0 |
| 24 | Sector size | 512 |
| 26 | Sixteen partition starts, 4 bytes each | Tracks 0, 16, and 516. The rest are 0 |
| 90 | Loader record | Logical block 2, length 23 blocks |
| 96 | Bad-block table record | Logical block 1, length 1 block |

Each reserved-area record is a 4-byte block number and a 2-byte length.
That is why the loader sits at offset 90 rather than on a 4-byte boundary.

A new-style partition stores only its starting track. Its size is the
gap to the next nonzero start, in logical blocks. The last partition
runs to `cylinders * heads` tracks. On this image slice 2 is

```text
(1400 * 16 - 516) * 8 = 175072
```

logical blocks, which is the root filesystem's `s_fsize`. Slice 0 is
one cylinder: the label, the bad-block table, and the loader. It is not
a filesystem. Slice 2 is the root filesystem. Slice 1 is the span
between them. On this image that span starts at track 16, is 4000
blocks, and reads as zeros. It is not a second s5fs.

An old-style table (the flag clear) stores a 16-bit start track and a
16-bit block count in those same 4 bytes. This image uses the new table.

## Logical block to physical sector

Blocks per track are derived:

```text
(sectors_per_track - (sectors_per_track & 1)) * sector_size / 1024
```

With 17 sectors of 512 bytes that is 8. Absolute track `T` starts at
physical LBA `T * sectors_per_track`. Logical disk block `B` is

```text
track = B / blocks_per_track
index = B % blocks_per_track
LBA   = track * sectors_per_track + index * 2
```

and occupies that LBA and the next one. The last sector of the track
(`sectors_per_track - 1`) is the spare and is not part of this map.

Slice 2 starts at track 516, physical LBA `516 * 17 = 8772`. Filesystem
block numbers are relative to that slice, so filesystem block `N` is
disk block `516 * 8 + N`.

## The filesystem

The bytes behind that map are System V s5fs, big-endian, 1024-byte
blocks. `filsys` already knows this inode and directory format as
`sysvr2`. The superblock packing on a UNIX PC is not the 4-byte-aligned
layout that edition uses today. See `unixpc_filsys.md`.

The 512-byte superblock begins at byte 512 of the slice. On this image
that is LBA 8773, file offset `0x448a00`. The first 512 bytes of the
slice (LBA 8772) are zeros. Measured fields, big-endian:

| Offset in the 512-byte superblock | Field | Value |
|---|---|---|
| 0 | `s_isize` | 2737 |
| 2 | `s_fsize` | 175072 |
| 6 | `s_nfree` | 48 |
| 8 | `s_free[]` | 4-byte block numbers |
| 414 | `s_time` | `0x2156ac91` (1987-09-22 17:00:01 UTC) |
| 426 | `s_tfree` | 163297 |
| 430 | `s_tinode` | 42566 |
| 496 | `s_magic` | `0xfd187e20` |
| 500 | `s_type` | 2, meaning 1024-byte blocks |

`s_fsize` counts 1024-byte blocks and matches the slice length above.
`s_type` 2 is the System V code for that block size. There is no
`s_state` word. A 4-byte-aligned superblock would put `s_fsize` at
offset 4, the magic at 504, and `s_type` at 508. This one does not.

Inodes are 64 bytes and start at logical block 2. On the first track of
the slice that is still before any spare, so the inode table is contiguous
in the file. Inode 1 is at file offset `0x449000`. Inode 2, the root, is
at `0x449040`:

| Field | Value |
|---|---|
| Mode | `0x41ff` (directory, mode 0777) |
| Links | 13 |
| Size | 288 |
| First block address | three bytes `00 0a b1` = 2737 |

Reading those three bytes as a 4-byte address yields 700672, which is
past `s_fsize`. The address width is 3. Indirect blocks and the free
list use 4-byte big-endian block numbers.

Filesystem block 2737 is the root directory. Mapped from track 516 it is
physical LBA 14588, file offset `0x71f800`, and the following sector.
A flat read of "byte 512 of the slice, plus 2737 × 1024" is the wrong
place: 342 spare sectors lie between track 516 and that block. The
directory is 18 entries of 16 bytes (a 2-byte inode number and a
14-byte name):

```text
.  ..  bin  mnt  etc  dev  tmp  lib  lost+found
mnta  mntb  u  .profile  UNIX3.51  unix  usr  unix.OLD  UNIX3.51m
```

`.` is inode 2. The names are the check that a mapper found the real
root.

The 67 MB image (`3b1-hd-pdp8online.img`, 71303168 bytes) is the same
layout with the label's other geometry: 1024 cylinders, 8 heads, 17
sectors. Slice 0 is tracks 0–8. Slice 1 is tracks 8–633, 5000 blocks,
and has been written (it begins with the repeating bytes `6d b6 db 6d`
and is not an s5fs). Slice 2 is tracks 633–8192, 60472 blocks, and
`(1024 * 8 - 633) * 8` equals that filesystem's `s_fsize`. Its superblock
uses the same offsets: magic `0xfd187e20` at 496, `s_type` 2 at 500,
`s_fsize` at 2. The root inode is again mode `0x41ff`, here with 15
links, size 448, and first block 946. The directory has the same base
names plus `UNIX3.51a`, `Notes`, `floppy`, `.phdir`, `.history`, and
several archives. The bad-block table is empty on this disk too.

## Bad-block table

The table is one logical block, named by the volume home block. On this
image that is logical block 1, physical sectors 2 and 3, still before
the first spare. Its 32-bit sum is `0xffffffff`. The first 8 bytes are
`ff ff ff ff` followed by zeros: the checksum cell, with no further
table block and no bad sectors.

A populated table is not on this image. The public `iv -vt` line is
"cylinder C, track H, sector S, uses track A, sector 16." Read that as
the bad sector `head * sectors_per_track + sector` on that cylinder, and
the replacement as the last sector of track `A` on the same cylinder.
One half of a 1024-byte block can move and the other stay. A second bad
sector on the same track uses some other track's spare. Nothing on this
disk confirms an alternate that points at another cylinder.

## What this file is not

The image has no separate swap header and no second filesystem with its
own superblock. Floppy images are a different layout. A second hard disk
is another raw image, selected by the machine, not a partition of this
file.
