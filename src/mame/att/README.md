# AT&T UNIX PC / 3B1 (`unixpc`)

Driver sources for the AT&T UNIX PC Model 3B1 live in this directory.

**Implementation notes, recipes, Diag Disk tables, WIN/3B / feth networking,
and HD/image/filsys writeups** are maintained in the lab companion repo (not
duplicated here for upstream hygiene):

- Lab: https://github.com/visionik/unixpc-lab  
- Hardware / board map: https://github.com/visionik/unixpc-lab/blob/main/docs/unixpc_hw.md  
- Disk image geometry: https://github.com/visionik/unixpc-lab/blob/main/docs/unixpc_image.md  
- Host filsys / SysV slices: https://github.com/visionik/unixpc-lab/blob/main/docs/unixpc_filsys.md  
- Branch changelog: https://github.com/visionik/unixpc-lab/blob/main/docs/changelog-att-3b1.md  
- Agent bootstrap: https://github.com/visionik/unixpc-lab/blob/main/README.md#for-agents-bring-up-3b1-in-mame  

Non-obvious hardware constraints that affect the code should stay as short
comments in the `.cpp` / `.h` files next to the logic they explain.
