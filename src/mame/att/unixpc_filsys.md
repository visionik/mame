# filsys changes for a UNIX PC disk image

The filesystem is the s5fs `filsys` already implements. The image is not
a flat range of those blocks. The work is a disk map in front of
`sysvr2`, plus the 68000 superblock packing that edition does not use
today. The image layout is `unixpc_image.md`.

Do not add a `3b1` or `unixpc` edition. A new `-v` name would fork the
descriptor for an inode format the code already has, and `-v unix` would
stop meaning "probe s5fs."

This checkout is filsys 5.0.0, unreleased. The 5.0 break is the public
header: on-disk types moved to `filsys_engine.h`, `stat` and directory
reads no longer use POSIX structs, and `filsys_readdir` is gone in favor
of `filsys_dir_seek` / `filsys_dir_next`. The native OpenBSD driver,
plan9, QNX, and macOS FSKit are a plan in `docs/openbsd-native-fs/`,
not code. They all sit on the same `filsys_io_t` the FUSE tools use.
The UNIX PC map wraps that vtable. It does not open the file and
`pread` on its own, or the later kernel transport cannot reuse it.
Tests list a directory with `filsys_dir_next`, not `filsys_readdir`.

Changing how a flat `sysvr2` image is decoded is a major version bump
(`VERSIONING.md`). `disk=unixpc` is the only path that clears `pack4`
and reads the magic at 496. A flat `sysvr2` open stays at offset 504.
The new files are internal. Nothing is added to `filsys.h`. filsys does
not ship disk images, so `3b1.hd` stays outside the tree. The range
check is `filsys_check_range()` against the slice's logical length, not
the raw file size.

## What the user types

```text
mount.filsys -v sysvr2 -o disk=unixpc,slice=2 -o arch=68k 3b1.hd /mnt
fsck.filsys  -v sysvr2 -d unixpc,slice=2 -a 68k 3b1.hd
findfs.filsys -d unixpc 3b1.hd
```

`arch=68k` is optional. The magic is big-endian `0xfd187e20`, and
`filsys_resolve_byteorder()` already trusts that word. `68k` is the
existing name (`68000` and `m68k` are aliases). It does not select the
UNIX PC superblock packing. Only `arch=pdp11` clears `pack4`, and that
arch is middle-endian, so it is the wrong switch.

`slice` is the partition index in the volume home block, the low 4 bits
of the UNIX PC disk minor. On `roms/3b1/3b1.hd`, slice 2 is the root.
`disk=unixpc` together with `-o offset=` (and fsck's numeric `-o`) is an
error. The slice start comes from the label.

fsck and findfs take `-d` because their `-o` already means something
else. Mount's `-d` is FUSE debug. The argument text is the same:
`unixpc,slice=N`. findfs may omit `slice` and scan every partition that
the label marks present.

## Two facts the current `sysvr2` path gets wrong

`FILSYS_SVR2` in `filsys_format.c` sets `pack4` and `magic_off` 504.
`probe_sysv()` in `v7fs.c` then hardcodes the 4-byte-aligned offsets:
magic at 504, `s_type` at 508, `s_fsize` at 4, `s_nfree` at 8, the free
list at 12, `s_ninode` at 212. `v7fs_open()` reads `s_type` from
`V7_SYSV_TYPE_OFF` (508) and the magic from `V7_SYSV_MAGIC_OFF` (504).

The UNIX PC superblock, measured at LBA 8773 of `3b1.hd`, is the
2-byte-aligned struct with `s_dinfo` present:

| Field | UNIX PC offset | `sysvr2` with `pack4` |
|---|---|---|
| `s_fsize` | 2 | 4 |
| `s_nfree` | 6 | 8 |
| `s_free[]` | 8 | 12 |
| `s_time` | 414 | 420 |
| `s_tfree` | 426 | 432 |
| `s_magic` | 496 | 504 |
| `s_type` | 500 | 508 |

`s_fsize` at offset 2 is 175072. The same bytes read as a 4-byte field
at offset 4 are `0xabe00030`. The magic at 496 is `0xfd187e20`. Byte 504
is not that word. `s_type` at 500 is 2 (1024-byte blocks).

The inode side already matches. `addr_width` stays 3. Inode 2 at file
offset `0x449040` stores its first block as the three bytes `00 0a b1`
(2737). `bo_get24be` is the decoder. Indirect blocks and the free list
stay 4-byte big-endian (`daddr_wid` 4). Directory entries stay 16 bytes.
Do not widen inode addresses to 4.

`sb_fsize_off(0)`, `sb_nfree_off(0)`, `sb_free_off(0)`, `sb_time_off()`,
and `sb_tfree_off()` in `v7fs.h` already describe the packed offsets
above when `pack4` is clear and `has_dinfo` is set. The open and probe
paths do not use them for the magic or for `s_type`. `probe_sysv()` does
not use them at all.

When `disk=unixpc` is bound, the edition remains `sysvr2` but the
descriptor for that open has `pack4` clear, `has_dinfo` set, magic at
496, and `s_type` at 500. `probe_sysv()` has to take those offsets from
the descriptor. A flat `sysvr2` open, with no `disk=` option, keeps
today's 4-byte-aligned layout.

## The disk map

New `unixpc.c` and `unixpc.h`, added to `libfilsys_a_SOURCES`. One
`filsys_io_t`. Mount, fsck, and findfs all use it. The FUSE adapters do
not learn about cylinders.

`unixpc_parse_vhb()` reads physical sector 0 through the inner
`filsys_io_t`, before the slice translation is installed. The label is
not behind the map. Fail, with a static reason, on a bad magic,
a 32-bit sum other than `0xffffffff`, a sector size other than 512,
`psectrk < 2`, `pseccyl` not equal to `heads * psectrk`, or a slice
start past `cylinders * heads`. Geometry comes only from the label. A
zero start track means "absent" for every slice except slice 0.

`unixpc_slice()` returns the absolute logical origin and the block
count. New-style size is the gap to the next start. Old-style size is
the stored count, and it must not cross the next start. A size of 0 or
a slice that runs off the image fails. For the `3b1.hd` header, slice 2
is origin `516 * 8` and length 175072.

`unixpc_map_block()` takes the parsed geometry and one absolute logical
block, and returns two physical 512-byte LBAs:

```text
blocks_per_track = (psectrk - (psectrk & 1)) * sectorsz / 1024
track = block / blocks_per_track
index = block % blocks_per_track
first = track * psectrk + index * 2
```

Reject a block past the disk, and reject a result whose sector index
within the track is `psectrk - 1`. That sector is the spare.

The bad-block table is the block named by the label (`resmap` index 1).
On this family it is logical block 1, physical sectors 2 and 3. Check
its 1024-byte sum. The first cell is the checksum (`altblk == 0` means
no further table block). Later cells are four big-endian ushorts:
cylinder, physical sector within the cylinder, alternate track, and the
next cell on that cylinder. The bad sector is `head * psectrk + sector`.
The replacement is the last sector of that track on the same cylinder.
Apply this per 512-byte half, so the two LBAs of one logical block need
not be adjacent. Fail a `nxtind` cycle, a cell that leaves the table or
hits cell 0, a cell that names another cylinder, or a replacement that
is itself listed bad. This image's table is empty. The alternate rule
is the `iv -vt` reading, not a populated table on `3b1.hd`. Do not guess
a second encoding. Fail closed.

The `filsys_io_t` read and write treat `off` as a byte offset in the
selected slice. The slice origin is added inside the map. Split the
request on 512-byte logical sectors, map each half, and call the inner
`filsys_io_t` at those physical byte offsets. A normal write must not
modify a spare. A write may modify a spare only when that spare is the
alternate for a sector in this request. A failed map, a short read, or
an offset past the slice returns an error and does not fall through to
a flat read.

## Call sites that still read the raw file

`v7fs_open()` in `v7fs.c` assigns `fs->io = &filsys_io_file` and checks
`fsize * bsize` against the raw file size. If a UNIX PC map is bound,
keep that io, set `fs->base` to 0 in the slice view, and limit the
filesystem to the slice's logical length. A filesystem that fits in the
physical file but not in the slice is rejected. The check as it stands
happens to pass for slice 2 of the 186 MB image, including a corrupt
`s_fsize` that merely fits in the file.

`filsys_resolve_byteorder()` in `filsys_format.c` raw-`pread`s four
bytes at `magic_byte_offset() + offset`. For `sysvr2` that is
`512 + 504`. On this disk the magic is at slice byte `512 + 496`, and
only after the spare-sector map. The function has to use the bound io.
Leaving it linear rejects a good disk, or accepts a flat misread of the
first track.

`filsys_detect()` and findfs call `probe_sysv()` with `&filsys_io_file`.
The probe has to receive the bound io and the slice's logical length as
`nbytes`. `probe_sysv()` also has to stop hardcoding offsets 4, 8, 12,
212, 504, and 508, and use the descriptor's `pack4`, `has_dinfo`,
`magic_off`, and type offset. Otherwise the mapped read still fails the
magic test.

`lock_image()` in `filsys.c` locks `[offset, offset + fsize * bsize)`.
The physical span of a slice is longer by one spare sector per track.
Lock that span. Two read-write opens of the same slice conflict. Slice
1 and slice 2 do not. fsck does not take this lock today. Leave that.

`unixpc_bind(fs, fd, slice)` is called from `filsys_open_arch()` and
from fsck's own `v7fs_open()` site. fsck does not go through
`filsys_open_arch()`. Bind before any superblock read, including the
byte-order probe and `filsys_detect()`.

`filsys_bmap_ino()` returns a filesystem-logical byte address and is
only used by tests. Leave it logical.

## Tools and pages

Mount accepts `-o disk=unixpc` and `-o slice=N`, and rejects them
together with `offset=`. fsck and findfs accept `-d unixpc,slice=N`.
findfs prints a mount line only for a slice whose translated probe is a
real `sysvr2` hit. A near-miss stays a near-miss. No flat fallback.

Update `mount.filsys.1`, `fsck.filsys.1`, `findfs.filsys.1`, and the
`-o` paragraph of `filsys.5`.

## Tests

`make check` runs `test_matrix`. Assertions are `ok("phrase", cond)`.
The default run must not need `3b1.hd`. Do not use
`--enable-image-tests`, which fails the run when an image is absent.

1. Header bytes copied from sector 0 of `3b1.hd` parse as 1400/16/17,
   slices at tracks 0, 16, and 516, loader block 2 length 23, bad-block
   table block 1 length 1. A flipped checksum byte fails. A second
   hand-built header (for example 32 cylinders, 4 heads, 17 sectors)
   parses to those numbers and is not special-cased.
2. On 1400/16/17, disk block `516 * 8 + 2737` maps to LBAs 14588 and
   14589. On 32/4/17, block 0 maps to LBAs 0 and 1, and block 8 maps to
   LBA 17, not 16. One block past the disk fails.
3. Slice 2 of the real header is origin `516 * 8` and length 175072.
   An old-style count that crosses the next start track fails.
4. A synthetic bad-block table with one cell. A read of the bad sector
   returns the spare's payload. A `nxtind` cycle fails. A replacement
   that is itself listed bad fails. Do not take an example from
   `3b1.hd`. It has none.
5. A temp image whose geometry is not 1400/16/17, whose slice does not
   start at track 0, and whose root block sits past at least one spare.
   The directory entries are `bin`, `etc`, `usr`, `unix`, `UNIX3.51`.
   Open read-only through `disk=unixpc` and list those names. A raw read
   at the linear superblock-plus-`isize * 1024` offset does not contain
   them. `fsck.filsys -n -v sysvr2 -d unixpc,slice=N` reports no errors.
   `findfs.filsys -d unixpc` prints `sysvr2`, `disk=unixpc`, and that
   slice. The same image opened with no `disk=` option must not succeed
   as a flat mount. The superblock in that fixture uses the packed
   offsets (magic at 496, `s_fsize` at 2, `s_type` 2 at 500).
6. Read-write on another temp image. Write a file and read it back.
   Spare sectors filled with `0xa5` beforehand are unchanged. A
   synthetic bad sector receives the write in its alternate, and the
   bad physical sector is unchanged. A second read-write open of the
   same slice returns `-EBUSY`. Two slices both succeed. Do not open
   `3b1.hd` read-write in any test.
7. If `FILSYS_UNIXPC_IMAGE` is unset, skip. If it is set, open that path
   read-only and check the label's own geometry, that slice 2's derived
   length equals `s_fsize`, and that inode 2 is a directory (`0x41ff`)
   whose entries include `bin`, `etc`, `usr`, `unix`, and `UNIX3.51`.
   Do not require 13 links or size 288. Those are the 186 MB root. The
   67 MB root is the same kind of directory with 15 links and size 448.
   A missing file when the variable is unset is success. A set variable
   pointing at a missing or wrong file is a failure.

## Out of scope

Floppy images. The second hard disk and the MCR2 select bit. MAME, the
WD1010, and any container other than the raw sector array. A new s5fs
inode or directory codec. `mkfs` of a volume home block. Formatting, and
rewriting the bad-block table. Teaching the FUSE adapters about tracks.
Mounting the live emulator image read-write, including as a test step.
