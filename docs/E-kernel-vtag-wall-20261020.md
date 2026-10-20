# E — O muro do `v_tag`/APR: por que o `.pkg` do USB não monta (FW 4.03)

**Data:** 2026-10-20 · **Sessão:** `external-pkg-hook` · **Estado:** bloqueio de kernel documentado

Este documento consolida **tudo** que foi descoberto na tentativa de montar o `.pkg`
original direto do USB (exFAT) e implementar o hook de kernel escolhido para liberar o
check `v_tag`. Todo o material é reproduzível a partir dos artefatos em
`/tmp/opencode/diag/` e das árvores clonadas em `/tmp/opencode/kstuff-src/`.

---

## 1. Resumo executivo

O caminho de montagem do pacote está **100% mapeado e funcional até o último check do
kernel**. O kernel recusa montar pacotes cujo arquivo vive em filesystem sem suporte a
**APR** — na prática, **só `bfs`** (o `/user` interno) passa. exFAT (USB), msdosfs,
tmpfs e nullfs são rejeitados; devfs/ufs/udf falham antes do check.

O hook de kernel no check (`get_package_vnode` → `v_tag(%s) can't support APR`) exige
**ler o texto do kernel** para localizar o branch. Nesta FW (4.03), **nenhum primitivo
disponível no userland** consegue ler ou escrever o texto do kernel: DMAP tem um buraco
exatamente na imagem, o pmap dos payloads não mapeia o texto, `mdbg` é no-op,
`/dev/dmem0` nega mmap, e o pipe `kernel_copyout` **derruba o console** ao tocar no VA do
texto (inclusive a partir do `SceShellCore` via ps5debug-NG).

Conclusão: a rota do hook fica bloqueada até existir uma primitiva **in-kernel** (kstuff
com leitura pelo CR3 do kernel — a dev build 1.13 possivelmente tem, mas o source não
está disponível). O único caminho funcional hoje é hospedar o pacote em `bfs` interno.

---

## 2. O gate do kernel (evidência)

Mensagem de erro (klog, CPU0) quando o arquivo está no USB:

```
[0]setup_mount_option() line=2970 mkeymode=GD
[0]check_mount_option_ppr_pkg() line=2914 sblock_offset[0x51520000]
[0]read_verify_sblock_ppr() line=1466 sblock_offset[0x51530000],
    container_offset[0x51530000], inner_sblock_offset[0xaea80000]
[0]get_package_vnode() line=1032 v_tag(exfatfs) can't support APR
read_verify_sblock_ppr() line=1558 error=45 0x2d
check_sblock() line=3230 error=45 0x2d
mountppfs() line=3441 error=45 0x2d
ppr_pfs_mount() line=656 error=45 0x2d
```

`error=45` = `EOPNOTSUPP`. O check é sobre o **vnode do arquivo do pacote**, antes de
qualquer leitura de conteúdo (provado com dummy de 1 MiB contendo só o FIH real).

### 2.1 Matriz de `v_tag` (empírica)

| hospedagem | resultado |
|---|---|
| `bfs` (`/user`) | **passa** → segue para `verify_ppr_sblock_100` (que o latch PPRPLAIN falsifica) |
| `exfatfs` (USB) | `can't support APR` |
| `msdosfs` (imagem FAT32 via LVD) | `can't support APR` |
| `tmpfs` (`/mnt`) | `can't support APR` |
| `nullfs` (dir do USB aliasado em path bfs) | `v_tag(null) can't support APR` |
| `devfs` (`/dev/lvdN`) | bloqueado antes: o attach interno do wrapper exige `st_size > 0`; device node stat = 0 |
| `ufs` (`/system_data`) | attach LVD falha (`ENOMEM`) antes do check |
| `udf` | driver não monta em LVD em nenhum `img_type` testado |

---

## 3. Contrato do wrapper `sceFsMountPprPkg` (provado em campo + RE)

RE do `libSceFsInternalForVsh.sprx` decifrado (dump em
`/tmp/opencode/diag/p1-run/fsinternal.dec.elf`, 722 136 B) e validação ao vivo:

| campo do `opt` | significado |
|---|---|
| `+0x00` | `pfsImgFile` (path do `.pkg`) |
| `+0x08` | inner mount path (2º estágio) |
| `+0x10` | budgetid (string; o wrapper escreve "game") |
| `+0x18` | **offset do início do volume** (não é chave!) |
| `+0x20` | offset absoluto do sblock no arquivo |
| `+0x30` | inner sblock offset |
| `+0x38` | modo `0x101..0x106` |
| `+0x80` | outer mount path |
| `+0x88` | inner image path (`<outer>/pfs_image.dat`) |
| arg2 | `{ int32 handle_out; ... }` (`-1` = falha) |

VolumeSize = `st_size(pfsImgFile) − opt+0x18`. O wrapper executa **dois** `_pfsMountData`
(outer/nest + inner/app0) na mesma chamada.

Ferramenta: `/tmp/opencode/diag/p1-probe/p1_tool.c` (`mountpkg2` + `pprarm` + `pprfinal`).

---

## 4. PPRPLAIN (kstuff) — protocolo e estado

`kekcall(magic=0x505052504c41494e, mode, arg0, 0, arg2, arg3, nr=0x700000027)`.

| mode | ação |
|---|---|
| 11 / 12 | scope enter/leave (`KSTUFF_FPKG_SCOPE_PPR_MOUNT=3`) |
| 1 / 2 / 3 | begin / write (chunks de 16 B) / check |
| 9 | `PLAINTEXT_NOAUTH` (arma o latch) |
| 0 | finish |

Staging = FIH `0x1000` + sblock `0x5a0` (sblock em `FIH+0x20`). O staging do pkg
`PPSA03706` valida 100% (`check ret=0`, `noauth ret=0`).

---

## 5. Kstuff 1.12 — build, instrumentação e o crash (resolvido)

Árvore: `EchoStretch/kstuff-lite` branch `1.12` (clone em
`/tmp/opencode/kstuff-src/kstuff-lite`). Build:

```sh
sudo pacman -S --needed yasm nasm
git submodule update --init --recursive      # BearSSL/isa-l/libtomcrypt
# (BearSSL: clonar master se o commit pinado não existir)
cd ps5-kstuff && PS5_PAYLOAD_SDK=/tmp/opencode/sdk/prebuilt/ps5-payload-sdk make
```

Loader mínimo (embute `payload.bin`, mantém o processo vivo):
`/tmp/opencode/kstuff-src/kstuff-lite/ldr-min/` → `kstuff-diag.elf`.

Instrumentação adicionada (não existia na 1.12):
- `kekcall` **nr 8** = `kread(dst_user, kva, size)` até 64 KiB;
- `kekcall` **nr 9** = publica `kdata_base`;
- campo `kdata_base_pub` no `shared_area` (reusa o `pad[16]`, tamanho inalterado);
- marcas `[kstage]` no `main.c` via syscall `0x259` (mesmo caminho do `klog_printf`).

### 5.1 Causa do crash inicial (registro honesto)

O payload público **crashava no `entry`**. O bisect (`[kstage] entry → dlsym → r0gdb=0 →
pre-kekcheck → kekcheck-ok → shellcore-begin → eh_frame → patches-done → dbg-enter-done →
syshook-* → kmalloc → ctable-* → shared-* → cpu0 → uelf-* → idt-begin`) revelou que a
falha era **minha adição**: publicar `kdata_base_pub` com `dereference` normal. O KELF
roda em **user mode**; escrever em VA de kernel exige o gadget `kmemcpy`. Corrigido:

```c
uint64_t kd = kdata_base;
kmemcpy((char*)shared_area_kernel + offsetof(struct shared_area_layout, kdata_base_pub),
        &kd, sizeof(kd));
```

Depois disso o kstuff 1.12 inicializa **100%** (r0gdb, shellcore patches, syscall hook,
IDT, UELF) e permanece vivo.

Observação: `klog_printf` em rajada é **rate-limited/dropado**; para diagnóstico usar
arquivo (`/mnt/usb0/kdiag.log`) ou sleeps entre linhas.

---

## 6. Localização exata do check (FW 4.03)

Com leitura de kernel funcionando (via SDK, página a página, DMAP para dados), a string
foi encontrada:

- `can't support APR` → **`kdata_base + 0x33EA07`**
- format string completa `"[0]%s() line=%d v_tag(%s) can't support APR\n"` →
  **`kdata_base + 0x33E9ED`**
- `kdata_base` = `payload_args_t::kdata_base_addr` (fornecido pelo elfldr) =
  `KERNEL_ADDRESS_DATA_BASE`
- `TEXT_BASE = kdata_base − 0xC00000` (FW 4.03; tabela do SDK)

O **código** que referencia essa string está no texto do kernel — inacessível (ver §7).

---

## 7. Matriz de primitivas de kernel (FW 4.03)

| primitiva | origem | leitura de **dados/rodata** | leitura do **texto** | escrita |
|---|---|---|---|---|
| `kernel_copyout/in` (SDK, pipe) | payload userland | funciona | **panic** | não testada (mesmo caminho) |
| `kernel_proc_getpaddr` + DMAP | SDK/CR3 do processo | funciona (dados) | N/A: **buraco físico** `0xf9f0000–0x105e0000` | idem |
| `mdbg_copyout` | SDK | retorna 0 **sem copiar** (no-op em retail) | no-op | no-op |
| `/dev/dmem0` | device | `read` = ENOTSUP; `mmap` = EACCES | idem | idem |
| `/dev/kmem` | — | **não existe** | — | — |
| `ps5debug-NG` `kern_read` (dentro do SceShellCore) | payload 1.3.2, porta 744 | — | **panic** (console caiu) | `kern_write` = mesmo primitivo |
| kstuff `copy_from_kernel` (kekcall 8) | UELF/kernel payload | funciona | **panic** (DMAP físico do texto) | — |

O pipe primitivo **não valida o endereço**: qualquer página não mapeada no contexto
corrente derruba o kernel. É a razão dos panics registrados.

---

## 8. Caminhos alternativos testados (e por que falharam)

### 8.1 ShadowMountPlus (SMP)
- Reconhece `.ffpfsc` como PFS aninhado; monta com `fstype=pfs`, `sigverify=0`,
  `mkeymode=AC`, `budgetid=game`, `ekpfs` zero.
- **Força read-only** para PFS aninhado (`mount_mode_forced`), portanto não escapa do
  check de APR.
- `nmount rdonly failed: Operation not supported` = o mesmo `v_tag`.
- O dir de título (`mount.lnk`) é apagado pelo sistema em ~3 s quando o TID não está
  registrado; titles registrados (criados pelo SMP) persistem.
- Config em `/data/shadowmount/config.ini`; API `POST /api/v1/scan` com
  `{"reset_attempts": true}`.

### 8.2 Dev build `kstuff-fpkg-1.13-dr-test6`
- Instala o patch A53 `Native/PLAINTEXT_NOAUTH` com sucesso ("PPR patch installed
  successfully"), patcha `app.db` e a ShellUI.
- **Não** altera o check de `v_tag` — o ENOTSUP continua.
- Fonte não disponível (referencia `/mnt/c/dev/ps5/kstuff-int3/`).
- Preservada em `/tmp/opencode/diag/kstuff-dev/` (md5 `06c3482f…`).

### 8.3 ps5debug-NG
- `kern_base` funciona (retorna `KERNEL_ADDRESS_DATA_BASE`).
- `kern_read` no texto = panic. Sem validação de endereço.

---

## 9. Estado atual do console (restaurado)

- dev build 1.13 carregada manualmente pelo usuário; marcador
  `/data/.kstuff_noautomount` recriado (estado original).
- `/user/app/PPSA03706` (instalação interna) **removida** a pedido do usuário;
  backup em `/mnt/usb0/PPSA03706-installed-copy.pkg`.
- pkg movido para `/mnt/usb0/jogos/ffpfsc/PPSA03706.ffpfsc`.
- Console caiu após o teste de `kern_read`; reboot pendente.

---

## 10. Opções abertas

1. **bfs interno (funciona hoje):** copiar o pkg para `/user` e montar/rodar. Viola a
   premissa "sem pkg no SSD interno", mas é o único caminho sem kernel patch.
2. **Source da dev build (`kstuff-int3`):** implementar o hook no contexto de kernel
   dela (que tem leitura pelo CR3 do kernel) com as condições do gate (d).
3. **Primitiva in-kernel própria:** um KELF mínimo que leia o texto pelo CR3 do kernel
   (requer o mesmo tipo de build do kstuff).

---

## 11. Artefatos

| path | conteúdo |
|---|---|
| `/tmp/opencode/diag/p1-run/fsinternal.dec.elf` | módulo VSH decifrado (wrapper) |
| `/tmp/opencode/diag/p1-re/findings2.md` | contrato do opt / VolumeSize |
| `/tmp/opencode/diag/p1-re/findings3.md` | scan/automount do SMP |
| `/tmp/opencode/diag/kstuff-dev/` | dev build 1.13 + a53 patcher preservados |
| `/tmp/opencode/kstuff-src/kstuff-lite/` | árvore 1.12 + instrumentação |
| `/tmp/opencode/kstuff-src/kstuff-lite/ldr-min/` | loader mínimo + `kstuff-diag.elf` |
| `/tmp/opencode/diag/p1-probe/p1_tool.c` | ferramenta (mountpkg2, ppr*, kpeek, kscan2, kwalk, kmdbg, dmmap…) |
| `/tmp/opencode/ps5debug-NG.elf` | debugger 1.3.2 (kern_base funciona; kern_read no texto = panic) |
