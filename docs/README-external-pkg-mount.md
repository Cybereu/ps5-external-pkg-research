# README — Mount direto de `.pkg` externo no PS5 (FW 4.03)

**Data:** 2026-10-19 · **Sessão:** pesquisa/experimento `external-pkg-hook` · **Status:** P1 DECIDIDO

---

## 0. Atualização 2026-10-20 — o muro do `v_tag`/APR (leia antes de tudo)

O caminho de montagem está **completo até o último check do kernel**. O kernel recusa
montar pacotes cujo arquivo vive em filesystem sem suporte a **APR**: na prática só
**`bfs`** (`/user`) passa; exFAT (USB), msdosfs, tmpfs e nullfs são rejeitados.

O hook de kernel no check exige **ler o texto do kernel**, e nesta FW (4.03) **nenhum
primitivo de userland** consegue isso: DMAP tem buraco na imagem, o pmap dos payloads
não mapeia o texto, `mdbg` é no-op, `/dev/dmem0` nega mmap, e o pipe `kernel_copyout`
**derruba o console** ao tocar no VA do texto (inclusive dentro do `SceShellCore` via
ps5debug-NG).

Detalhes completos, offsets exatos (`can't support APR` em `kdata_base+0x33EA07`),
matriz de primitivas e artefatos: **[`research/E-kernel-vtag-wall-20261020.md`](research/E-kernel-vtag-wall-20261020.md)**.

Rotas possíveis hoje: (1) hospedar o pkg em `bfs` interno (funciona); (2) implementar o
hook no source da dev build `kstuff-int3` (contexto de kernel próprio); (3) primitiva
in-kernel própria.

---

## 1. Resumo executivo — COM A RESSALVA HONESTA (2026-10-19)

**O que está provado:**
- O `.pkg` original (FIH/FPKG debug PLAINTEXT-NOAUTH) tem a estrutura **validada byte
  a byte** e o **handshake PPRPLAIN do kstuff valida 100%** (staging OK, latch armado).
- O **launch nativo do prefixo idêntico** (`app.pkg` interno ≡ prefixo do USB) monta e
  faz o parse NAPS completo **sem erros de container** ⇒ o conteúdo do pkg é
  processável pelo pipeline nativo ⇒ **P1 = V1a** (redirect de path) é o vetor indicado.

**O que NÃO está provado (correção de registro):** o `sceFsMountPprPkg` standalone
**ainda não monta** o pkg — devolve `handle=-1` (falha silenciosa) em todas as
configurações testadas (chaves EKPFS derivadas, geometry fills, latch, convenções de
buffer). Detecções anteriores de "mount sucesso" eram **fantasma** — o `statfs=87` é o
fsid do tmpfs (linha de base `/mnt` idêntica). Próximo passo: RE do módulo decifrado
no console (rota AuthMgr/etaHEN) para achar a condição de falha do wrapper, ou os
vetores (b)/(c) do Oracle.

```c
// chamada real (a convenção do buffer é {handle_out; path} — handle em +0!)
uint8_t pathbuf[256] = {0};
strncpy((char*)pathbuf + 4, "/mnt/p1mnt", ...);
sceFsInitMountPprPkgOpt(opt);   /* opt: pfsImgFile @+0, budgetid @+72 */
sceFsMountPprPkg(opt, pathbuf, ekpfs_32B, 0, 0, 0);
/* pathbuf[0..3] = handle; -1 = falha (o que observamos) */
```

## 2. Objetivo original

Rodar o `.pkg` original direto do HD/USB externo num PS5 jailbroken (FW 4.03), sem
extrair/converter e sem manter o pacote completo no SSD interno (stubs pequenos são
permitidos). Dump-folder é uma vertente separada, não a preferência.

## 3. Receita de mount (a descoberta principal)

Estrutura do opt (layout confirmado pelo único chamador público,
[`hzhreal/PS4-vsh-utils`](https://github.com/hzhreal/PS4-vsh-utils) `source/pkg.c`; o
`Init` do PS5 preenche mais campos — usar buffer zeroado de ≥256 B):

```c
typedef struct {            /* início comum (PS4/PS5) */
    const char *pfsImgFile; /* +0  caminho do .pkg */
    uint8_t     cache[64];  /* +8  preenchido pelo Init */
    char       *budgetid;   /* +72 "system" ou "game" */
} mount_pkg_opt_t;

uint8_t opt[256] = {0};
sceFsInitMountPprPkgOpt(opt);
*(const char **)opt  = pkg_path;
*(char **)(opt + 72) = "system";
uint8_t key[64] = {0};      /* crypto é emulada pelo kstuff — chave irrelevante */
sceFsMountPprPkg(opt, mountpoint, key, 0, 0, 0);
```

**Por que funciona:** o wrapper do sistema faz o pipeline completo internamente (LVD
attach + conjunto de opções + interação com o hook do kstuff/PPRPLAIN). Tentativas de
reproduzir o `nmount` manualmente falham em `check_mount_option_ppr_pkg` (2909) /
`mountppfs` (3394 EROFS) — ver §5.

## 4. O tool de sondagem (`p1_tool`)

Payload ELF (`/tmp/opencode/diag/p1-probe/`, build: `PS5_PAYLOAD_SDK=… make`; TDD com
169 checks). Comandos principais:

| Comando | Função |
|---|---|
| `mountpkg <pkg> <mnt> [game\|ppr]` | mount via API do sistema (`ppr` é o que funciona para FIH) |
| `umountpkg <mnt> [game\|ppr]` | unmount via API (pendência: path no opt ainda não bate — rollback feito por `umount` do shsrv) |
| `lvdattach <path> <sector> <img_type> [option_len] [secondary]` | attach LVD manual (ioctl `0xC0286D00`) |
| `lvddetach <id\|last>` | detach por `device_id` (com limpeza de `last`) |
| `mntfs <fstype> <src\|last> <mnt> [opções…]` | `nmount` com pares `(nome, valor)`; flags avulsas → par `(nome, NULL, 0)`; diretivas `@rdonly`/`@force` viram flags do syscall |
| `rmdir <path>` | remove com curinga (`p1mnt*` tira o dir-lixo `p1mnt\n`) |

Diagnóstico embutido: o buffer `errmsg` é **writeback do kernel** e é impresso após cada
`nmount` (`ERRMSG> …`) — foi ele que revelou `mount option <X> is unknown`.

## 5. Mapa de diagnóstico do `nmount` (para quem for depurar)

| Sintoma no klog | Significado |
|---|---|
| `check_mount_option_ppr_pkg() line=2909 error=22` | conjunto de opções rejeitado: qualquer indicação de RO (`rdonly`, `ro`, `MNT_RDONLY`) ou flags avulsas mal-pareadas |
| `mount option <X> is unknown` (via errmsg) | nome de opção inválido — nomes válidos: `mkeymode, budgetid, sigverify, playgo, disc, ekpfs, errmsg, sblock_offset, key_ver, finalized, pubkey_ver, ppkg_opt, casesensitive=1, async, noatime, automounted, force` |
| `mountppfs() line=3394 error=30 (EROFS)` | device anexado no perfil RO (`option_len=0x1C` = `SINGLE_RW\|RO_VARIANT`) e mount pede escrita |
| `lvd_validate_option() failed / Inconsistent options` | perfil de attach inválido para o `img_type` (tipos válidos 0–12; `2`=GmO game-outer; `0x82` NÃO é tipo) |

Constantes do LVD (fonte: `sm_mount_defs.h` do ShadowMountPlus — **código aberto**, espelhos
públicos): `0x1C` = `SINGLE_RW|RO_VARIANT` (RO), `0x14` = `SINGLE_RW` (RW), `0x04` = BASE;
`secondary_unit` otimizado = `0x10000`; `errmsg` = buffer de 256 B com valor vazio.

## 6. O que o pkg realmente é (verificado byte a byte)

O `.pkg` da PS5 é uma imagem finalizada `\x7FFIH` (header LE):

| Offset | Campo | Valor no pkg do Peppa |
|---|---|---|
| 0x00 | magic | `\x7fFIH` ✓ |
| 0x05 | signed byte | `0x00` (debug) |
| 0x10 | PFS image offset | `0x10000` ✓ |
| 0x18 | PFS image size | `0x51640000` |
| 0x20 | sblock offset | `0x51530000` (validado: u64[0]=2, ver `0x1332a0b`, modo `0x000d`, block `0x10000`) |
| 0x58 | CNT (SC) offset | `0x51650000` (byte a byte: header `\x7FCNT` + content-id) |
| 0x68 | outerTweakBase | `0x800000000000` |
| 0xA0/0xA8 | napsMetadata Offset/Size | `0x514d0000` / `0x494a8` |
| sblock+0x370 | seed marker | **`PPRPLAIN-NOAUTH!`** ⇒ o pkg é do flavor **PLAINTEXT-NOAUTH** (pipeline `PLAINTEXT_NOAUTH` do LibProsperoPkg/FPKG-Builder) |

Estrutura interna: FIH → PFS externo (`uroot`: `pfs_image.dat`, `naps_pkg_layout.dat`) →
PFS interno → app0. O `app.pkg` instalado internamente ≡ prefixo `[0, 1394999296)` deste
arquivo (1397168333 B) — o mount nativo do launch usa os mesmos bytes.

## 7. Pendências (não bloqueiam o veredito)

1. **`umountpkg`**: o opt do unmount precisa do path em campo próprio (`Mount path not
   specified`); rollback atual é garantido pelo `umount` do shsrv.
2. **Raiz vazia no `readdir`**: o superroot do PFS não expõe `uroot`/`pfs_image.dat` via
   readdir — a visão correta é o nível interno (a confirmar com o launch flow).
3. **`img_type` exato do pkg bruto** no attach manual: candidatos `{2, 9, 8, 5, 10, 11}`
   (attach aceita todos; `0x82` é rejeitado).

## 8. Próximos passos

- **V1a (vetor indicado):** desenho do redirect de path no launch — **gated** (revisão do
  Oracle antes de qualquer hook de kernel/redirect).
- **Endgame alternativo (SMP/fakelib):** desinstalar a cópia interna → a fonte USB vira
  montável via SMP (padrão já usado pelo usuário: Horizon roda de imagem `.exfat` no USB,
  Astro/SMB3D de pastas, tudo via SMP + `fakelib_mode=full`).
- Integração no **app** (`app/` — PS5 Backport Doctor): check de diagnóstico do mount de
  pkg externo com base nesta receita.

## 9. Protocolo PPRPLAIN do kstuff (implementado no tool)

Para montar um pkg do flavor **PLAINTEXT-NOAUTH** é preciso o handshake do kstuff
(`ps5-kstuff/uelf/fpkg.c` — o `ShellCore wrapper` falha sem ele):

1. `kekcall("PPRPLAIN", SCOPE_ENTER=11, v=8, kind=3)` — scope `PPR_MOUNT`;
2. `BEGIN=1` → `WRITE=2` (staging: FIH 0x1000 + sblock 0x5a0, em chunks de 16 B) → `CHECK=3`;
3. `PLAINTEXT_NOAUTH=9` — valida o staging e **arma o latch** (pass-through do XTS);
4. `sceFsMountPprPkg(...)` — o mount usa o latch da thread;
5. `finish=0` + `SCOPE_LEAVE=12`.

O kekcall é a syscall `0x700000027` (nr 7 sobre a syscall 0x27 interceptada —
`kekcall.c` case 7). Comandos do tool: `pprcheck`, `pprarm <pkg>`, `pprfinal <ret>`.
No nosso pkg o staging valida **100%** (`check ret=0`, `noauth ret=0`).

**Estado do conteúdo:** o mount do wrapper funciona de forma estável e reproduzida,
mas expõe um volume **vazio** (~2 MB, 128×16 KiB) — matriz completa de parâmetros
testada sem sucesso: args de estágio (7 combinações), sufixos `-app0-nest`/`-app0`,
latch ativo, `volumeSize`/`sblock_offset` nas posições +8/+16/+24 do opt, e o
dois-estágios via `pfs_image.dat`. Hipótese ativa: o wrapper seleciona a região do
volume por um campo de opt não identificado — pendente o **RE offline do
`libSceFsInternalForVsh.sprx`** (SELF comprimido; lane dedicada em curso).

## 10. Evidências

- `/tmp/opencode/diag/p1-final-sysapi-20261019T162812440452Z/` — mount bem-sucedido + readback.
- `/tmp/opencode/diag/p1-tree-20261019T163031669149Z/` — reprodução + caminhada.
- `/tmp/opencode/diag/p1-exec-*`, `p1b-…`…`p2b-…` — as ~70 tentativas de opções (todas com rollback limpo).
- `/tmp/opencode/diag/p1-probe/` — tool + TDD (169 checks) + `PLAN.md`.
- `.slim/deepwork/external-pkg-hook/` — progresso e checklist (R1–R8).
