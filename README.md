# PS5 external .pkg research — the `v_tag`/APR wall (FW 4.03)

Research notes and tooling from an attempt to run an **original `.pkg` (FPKG/FIH) directly
from an exFAT USB drive** on a jailbroken PS5 (FW 4.03), as a normal installation — and
from the attempt to lift the single kernel check that blocks it.

**Outcome in one paragraph:** the mount pipeline is fully mapped and works up to the last
kernel check (`get_package_vnode()` → `v_tag(<fs>) can't support APR` → `EOPNOTSUPP`).
Empirically, only **`bfs`** (`/user`, internal SSD) satisfies that check; exFAT/msdosfs/
tmpfs/nullfs are rejected. Lifting the check requires reading the kernel's **text**, and
on this firmware every userland primitive available failed to do that safely (DMAP hole
over the kernel image, payload pmap without the kernel text, `mdbg` no-op, `/dev/dmem0`
read/mmap denied, and the `kernel_copyout` pipe **panics** on the text VA — including from
inside `SceShellCore` via ps5debug-NG). The hook is therefore **NO-GO on FW 4.03 with
these primitives**, unless an in-kernel primitive (or the dev-build source) is available.

Full write-up: [`docs/E-kernel-vtag-wall-20261020.md`](docs/E-kernel-vtag-wall-20261020.md).

---

## What is in this repository

| path | content |
|---|---|
| `docs/E-kernel-vtag-wall-20261020.md` | the consolidated technical report (gate, addresses, primitive matrix, artefacts) |
| `docs/README-external-pkg-mount.md` | earlier consolidated notes: wrapper contract, PPRPLAIN, mount recipe |
| `docs/decision-log.md` | decision record, including the kernel-hook gate and its revised verdict |
| `tools/p1_tool.c` (+ `parse.c`, `test_parse.c`, `Makefile`) | PS5 payload: `pprarm`/`mountpkg2`/`pprfinal`, kernel read helpers, diagnostics |
| `tools/analyze_hit.py` | disassemble a raw kernel-memory hit at its virtual address |
| `tools/kstuff-instrumentation/loader/` | minimal loader that embeds a kstuff `payload.bin` and self-runs diagnostics |
| `patches/kstuff-lite-1.12-instrumentation.diff` | our additions to `EchoStretch/kstuff-lite` 1.12 (kekcall 8/9 + `kdata_base` publication) |

## Key findings (FW 4.03)

- The wrapper `sceFsMountPprPkg` performs **two** stages in one call (outer/nest + inner/app0).
  `opt+0x18` is the **volume start offset** (not a key); `VolumeSize = st_size − opt+0x18`.
- `opt` map: `+0x00` pkg path, `+0x08` inner mount, `+0x10` budgetid, `+0x18` volume start,
  `+0x20` sblock file offset, `+0x30` inner sblock offset, `+0x38` mode (`0x101..0x106`),
  `+0x80` outer mount, `+0x88` inner image path; arg2 = `{int32 handle_out}`.
- PPRPLAIN protocol (`kekcall` nr 7): scope 11/12, begin 1, write 2, check 3, noauth 9,
  finish 0; staging = FIH `0x1000` + sblock `0x5a0`.
- The blocking check is `get_package_vnode()` (line 1032). The message string sits at
  `kdata_base + 0x33EA07`; the full format string at `kdata_base + 0x33E9ED`;
  `TEXT_BASE = kdata_base − 0xC00000`. `kdata_base` comes from the elfldr payload args.
- `bfs` is the only filesystem that passes; `devfs`/`ufs`/`udf` fail before the check.

See the report for the primitive-by-primitive evidence and the exact experiments.

## Building the tools

The payload is built with the PS5 payload SDK:

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
cd tools && make            # produces p1_tool.elf
```

The kstuff instrumentation patch applies to `EchoStretch/kstuff-lite` branch `1.12`:

```sh
git clone --branch 1.12 https://github.com/EchoStretch/kstuff-lite
cd kstuff-lite
git apply /path/to/kstuff-lite-1.12-instrumentation.diff
git submodule update --init --recursive    # BearSSL / isa-l_crypto / libtomcrypt
cd ps5-kstuff && make
```

`tools/kstuff-instrumentation/loader/` builds a payload that embeds
`../ps5-kstuff/payload.bin` and runs a self-contained diagnostic after the kernel payload
initialises.

## Notes and scope

- No Sony binaries, decrypted modules, game packages or third-party payload binaries are
  included. Offsets and strings quoted here were obtained from a console the author owns.
- Everything is documented for interoperability research; use only on hardware you own.
- The console-side state and the exact steps that produced each result are in the report.

## License

MIT — see [`LICENSE`](LICENSE).
