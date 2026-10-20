# tools

PS5 payload used during the research. Built with the PS5 payload SDK
(`export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk && make`).

## Commands (payload console over the elfldr socket)

| command | purpose |
|---|---|
| `pprarm <pkg>` | enter the PPRPLAIN scope, stream FIH+sblock staging, check, arm the latch (leaves the scope open for the mount) |
| `mountpkg2 <pkg> <outer_mnt> <inner_mnt> [vol_start]` | full two-stage `sceFsMountPprPkg` call with geometry derived from the FIH (`+0x10` volume start, `+0x20` sblock, `+0x50<<16` inner sblock) |
| `pprfinal 0` | finish + scope leave |
| `lvdattach` / `lvddetach` | LVD attach of an image file (`/dev/lvdN`) |
| `mntfs` | raw `nmount` with an option list |
| `kscan` / `kscan2` / `kpeek` / `kwalk` / `kphys` / `dmmap` / `kmdbg` | kernel-memory diagnostics (see the report for which ones are safe) |
| `kkread` / `kkxref` | reads through the instrumented kstuff kekcalls (8/9) |

`analyze_hit.py` disassembles a raw kernel-memory hit at its virtual address:

```sh
python3 analyze_hit.py <hit_va_hex> <hex_bytes>
```

No Sony binaries, decrypted modules or game packages are included.
