# Registro de Decisões

> **Status:** rascunho consolidado (síntese do orquestrador).
> **Escopo:** decisões tomadas, data, justificativa, evidências de apoio e alternativas descartadas.
> Convenções: ver `../README.md`.

## D1 — Fase 1 foi de pesquisa; sem implementação
- **Data:** 2026-10-11
- **Decisão (histórica):** durante a Fase 1 não se escreveu payload nem se modificou o sistema.
- **Justificativa:** imposição do escopo (seções 2 e 9); risco de corrupção/brick em FW antigo.
- **Evidências:** escopo do projeto; aviso de primeira mão do `ShadowMountPlus` (shutdown/corrupção).
- **Descartado:** prototipar payload antes de validar premissas.

## D2 — Idioma dos entregáveis: português (PT-BR)
- **Data:** 2026-10-11
- **Decisão:** prosa em pt-BR; nomes de arquivos/pastas e termos técnicos mantidos como estão.
- **Justificativa:** preferência do usuário.
- **Descartado:** documentos em inglês; bilíngue.

## D3 — Estrutura da base de conhecimento (seção 5), com `sources/raw-notes/`
- **Data:** 2026-10-11
- **Decisão:** adotar a estrutura sugerida, acrescentando `sources/raw-notes/` para capturas brutas de cada frente.
- **Justificativa:** preservar rastreabilidade (notas originais em inglês) separada dos documentos consolidados em pt-BR.
- **Descartado:** descartar as notas brutas; traduzir sem registrar origem.

## D4 — Rótulos de evidência e citação por fonte
- **Data:** 2026-10-11
- **Decisão (histórica):** adotou-se rótulos (Confirmado/Fortemente sustentado/Hipótese/Contradito/Desconhecido) e citação por fonte.
- **Justificativa:** exigência do escopo; evita tratar hipótese como fato.
- **Descartado:** afirmar sem rótulo/fonte.

## D5 — Tratamento de lacunas de documentação
- **Data:** 2026-10-11
- **Decisão:** lacunas ficam registradas como **Desconhecido**, com plano de verificação.
- **Evidências:** campos PS5 undocumented (formato PKG, `nsfs`, DB schema).

## D6 — Arquitetura recomendada (preliminar): H1 (imagem externa + nullfs), com H1b e `nsfs` como linhas alternativas
- **Data:** 2026-10-11
- **Decisão:** recomendar, como candidata principal, a arquitetura que espelha o padrão observado no sistema; manter H1b (sandbox-only) e RE do `nsfs` como alternativas.
- **Justificativa:** maior suporte de evidências (código do OS + ferramentas), alinhada ao split conteúdo/metadados do próprio sistema.
- **Evidências:** `app0` = nullfs de `/system_ex/app/<TID>` **[Fortemente sustentado — observado para NPXS40087; FW não especificado; extensão a jogos/4.50 = Hipótese]**; `ShadowMountPlus`/`kstuff-lite` **[Confirmado em código]**.
- **Ressalva:** **não verificada** em 4.50/4.51; risco documentado em FW antigo.
- **Descartado (por ora):** interceptação de E/S em kernel (D) — sem implementação pública, alto risco.

## D7 — Fase 2 começa por inspeção/diagnóstico; HDD externo nos experimentos de armazenamento
- **Data:** 2026-10-11
- **Decisão:** a Fase 2 inicia por **E-1 (bring-up das primitivas)** e E0/E1 (leitura/diagnóstico) e usa o USB externo nos experimentos de armazenamento.
- **Justificativa:** segurança e ordem do plano de experimentos.

## D8 — Reconciliar autoria do `ShadowMountPlus` (RESOLVIDO)
- **Data:** 2026-10-11
- **Decisão (resolvida):** citar **`drakmor/ShadowMountPlus`** como repositório canônico; `PSBrew/ShadowMountPlus` é apenas um fork downstream.
- **Evidências:** GitHub API em 2026-10-11 — `drakmor/ShadowMountPlus` (857★, push 2026-10-10, branch `1.7`, fork de `RDX-Sci01/ShadowMount1.3GBT`) vs `PSBrew/ShadowMountPlus` (1★, push 2026-06-04, parent = drakmor). 

## D9 — Revisão independente #01 aplicada (rótulos, contradições, riscos e plano de testes)
- **Data:** 2026-10-11
- **Decisão:** aplicar as correções da revisão independente #01 (`../reviews/oracle-review-01.md`).
- **Principais correções:**
  1. "`app0` = nullfs de `/system_ex/app/<TID>`" rebaixado de **Confirmado** para **Fortemente sustentado**, com escopo (observado para o título de sistema NPXS40087 em FW não especificado; extensão a títulos de jogo e a 4.50/4.51 = Hipótese); "arquitetura nativa do OS" reescrita como "espelha o padrão observado no sistema".
  2. Suporte declarado a 4.50/4.51 (umtx2, PS5-UMTX-Jailbreak, ps5-hen, bdj-ipv6-hen, etaHEN, kstuff) rebaixado para **Fortemente sustentado**; "offsets existem" mantido **Confirmado**.
  3. Contradições resolvidas: M.2 em 4.50/4.51 (suporta; FW ≥ 4.00), formatação exFAT vs *fstype* de kernel, kstuff-lite **v1.07+**, nullfs de userland, mitigação "nunca montar sobre path vivo" reescopada para o dispositivo de origem.
  4. Riscos ausentes **R1–R7** e alternativas **M1–M2** acrescentados.
  5. Plano de testes reparado: experimento de bring-up **E-1**, shutdown/power-cycle + checagem de mount obsoleto após shutdown sujo, auditoria de escrita, formato/ICV fixados em E2, escopo do **X2**, experimento **E-Layout**, critério de aborto falsificável e critérios de entrada adicionais.
  6. Taxonomia de rótulos limpa ("Contradito por ausência" → **Desconhecido**; rótulos híbridos normalizados para um rótulo + nota; ENOENT/EIO unificado em "Confirmado (FreeBSD) / Fortemente sustentado (PS5)").
- **Evidências:** `../reviews/oracle-review-01.md`.

## D10 — Objetivo do usuário: **instalar o pacote no volume externo** ("Modelo 1"); "Modelo 2" como alternativa e possível mecanismo de base
- **Data:** 2026-10-11
- **Decisão:** registrar como **objetivo primário** "**instalar/usar o pacote (formato Sony) no HD externo em vez do interno**" (**Modelo 1**). Manter "**montar a imagem do externo sem instalar**" (**Modelo 2**) como o **caminho com mais evidências** e possível **mecanismo de base** do Modelo 1.
- **Justificativa:** objetivo declarado pelo usuário; como a pergunta de desambiguação foi descartada, **ambos seguem como hipóteses** (não se força escolha).
- **Evidências:** `../architecture/alternatives.md` (A/C = Modelo 2; **F = Modelo 1**); `../research/open-questions.md` #20–#21; `../experiments/test-plan.md` **E-INST**; `../architecture/feasibility-matrix.md` (linha F).
- **Nota de terminologia:** sob jailbreak o pacote instalável costuma ser o **`.fpkg`** (debug/FIH, keyset fake); o **`.pkg`** é o container da Sony. Ver `../research/fpkg-format-and-installation.md`.

## D11 — Inspeção on-device (somente leitura) da instalação PPSA36082
- **Data:** 2026-10-11
- **Decisão:** realizar (e registrar) uma inspeção **somente leitura** do console-alvo via FTP anônimo (porta 1337) para analisar a instalação de `PPSA36082`. **Nenhuma escrita/instalação** feita pelo projeto.
- **Achados:** título instalado **internamente** (~7,99 GiB em `/user/app/PPSA36082/app.pkg`); pipeline = `sceAppInstUtilInstallByPackage` com stream em **localhost** (payload `pkgmgr`); FPKG confirmado (`app.json` `"fake":true`); **correção de schema** (`tbl_contentinfo` em `app.db`; `tbl_appinfo` em `appinfo.db`; sem `tbl_appbrowse_*`); existem campos de HDD externo; **Modelo 2 já operante** via `mount.lnk`→`/data/homebrew/<TID>`.
- **Impacto na base:** adiciona a seção 8 de `research/fpkg-format-and-installation.md` e a seção 7 de `sources/source-evidence.md`; resultado completo em `../experiments/results/E-install-PPSA36082.md`.
- **Consequência para o objetivo:** confirma que "instalar" grava interno; o caminho viável para "usar do externo" é a montagem (Modelo 2); o Modelo 1 passa a ter uma pista concreta (campos `hddLocation`/`externalHddAppStatus`).

Ver também: `../README.md`, `../architecture/proposed-design.md`, `../research/open-questions.md`.

## D12 — Perfil do dispositivo e Super Meat Boy (inspeção somente-leitura)
- **Data:** 2026-10-11
- **Decisão:** registrar a inspeção **somente leitura** (FTP porta 2121) do console-alvo durante os testes do usuário.
- **Achados:** (a) o console reporta **FW 4.03** (log do ShadowMountPlus) — **divergente** do FW-alvo 4.50/4.51 assumido; **reconciliar**; (b) **Super Meat Boy = PPSA31162**, FPKG (`\x7FIH`) no USB (base 2,68 GiB + `-4xx` 252 MB), já instalado no interno e **lançado com backport externo** pelo ShadowMountPlus; (c) confirmado o mapa dos mecanismos (instalar→interno; homebrew→`/data/homebrew`; dump/imagem→externo; backport→NSFS app0).
- **Impacto:** adiciona `../experiments/results/E-device-profile-and-SMP.md`; **FW-alvo da pesquisa em revisão**.
- **Consequência:** caminho de menor risco para "usar do externo" = converter o FPKG em **imagem montável** e montar via ShadowMountPlus (Modelo 2); investigar `BackPork`/`kstuff-fpkg`/`ps5-app-dumper`.

Ver também: `../README.md`, `../architecture/proposed-design.md`, `../research/open-questions.md`.

## D13 — Estratégia de bypass da restrição "instalar só no interno"
- **Data:** 2026-10-11
- **Decisão:** registrar as estratégias de bypass e recomendar **B1** (nullfs do conteúdo do USB sobre `/user/app/<TID>`) como a mais promissora; **B2** (registro via `mount.lnk`) como caminho **já comprovado**; **B3** (campos de HDD externo) como via nativa não demonstrada; **B4** (patch do instalador) como última opção.
- **Justificativa:** o instalador nativo não aceita destino externo; o OS **já usa nullfs** (`app0`) e o ShadowMountPlus **já publica conteúdo por nullfs** → B1 é o menor salto conceptual. B2 roda hoje (dumps/imagens).
- **Evidências:** `../architecture/proposed-design.md` (seção de bypass); on-device em `../experiments/results/E-device-profile-and-SMP.md` e `E-install-PPSA36082.md`.
- **Riscos registrados:** mount **não persiste** ao reboot (remontar via autoload); título quebra se o USB faltar; integridade/DRM exigem a camada de backport (NSFS app0).

## D14 — Canal de execução + primitivas de bypass provadas (sessão on-device)
- **Data:** 2026-10-11
- **Decisão:** prosseguir com experimentação on-device ("yolo"), mantendo mounts de teste reversíveis.
- **Achados:** (a) **canal de execução remota** de payloads via elfldr:9021 com stdout de volta + root/kernel R/W; (b) **nullfs do USB criado/destruído com sucesso** (primitiva do bypass); (c) **API do ShadowMountPlus** mapeada e dirigida (POST/JSON), com `move/copy/unpack source` e **`/mnt/usb0` como destino**; (d) `app0` = nullfs de `/system_ex/app/<TID>` e pkgs instalados são `can_manage_source:false`.
- **Consequência:** o bypass viável é **redirecionar o caminho de conteúdo para o USB via nullfs** (e/ou preparar o jogo como pasta/imagem para o SMP mover/rodar do USB). End-to-end ainda a validar.
- **Artefatos:** `../experiments/results/E-bypass-primitives-and-smp-api.md`; tooling em `/tmp/opencode/diag/` (tool.elf, explore.elf).

Ver também: `../README.md`, `../architecture/proposed-design.md`, `../research/open-questions.md`.

## D15 — SUCESSO end-to-end: jogo rodando do armazenamento externo
- **Data:** 2026-10-11
- **Resultado:** **Super Meat Boy 3D (`PPSA31162`) executando do USB** (`/proc/187_PPSA31162_eboot.bin`; conteúdo em `/mnt/usb0/Games/PPSA31162` publicado via **nullfs** em `/system_ex/app/PPSA31162` → `app0` do sandbox). Interno ficou só com metadados + `mount.lnk`.
- **Receita:** instalar (temporário) → **`ps5-app-dumper`** para o USB → desinstalar interno → **SMP `/manual/add`** da pasta do USB + `/scan` → lançar.
- **Consequência:** o **bypass é viável e demonstrado**. O instalador nativo não tem destino externo, mas o conteúdo pode residir no USB e ser servido via nullfs (B1/B2).
- **Limitações:** dump exige instalação interna temporária; mounts não persistem ao reboot (o SMP remonta); precisa de título lançável (backport).
Ver também: `../README.md`, `../architecture/proposed-design.md`, `../research/open-questions.md`.

## D16 — "pkg no externo como instalação normal" — redirect de caminho NÃO funciona
- **Data:** 2026-10-11
- **Resultado:** `bfs` (**bfs**) não suporta symlink; `nullfs` sobre `/user/app/<TID>` **monta**, mas o **console recusa lançar** o título (`lnc 0x80020019`) — reproduzível; sem o mount, lança. → redirecionar o diretório de instalação por mount/symlink **não** viabiliza "o `.pkg` no USB como instalação normal".
- **Consequência:** para esse objetivo são necessárias vias mais profundas: **interceptação em kernel** do aberto/LVD do `app.pkg` (B), **edição de `app.db`** (C) ou o **"move app to storage" nativo** (D). A via de **pasta via SMP** (A) continua comprovada (folder dump).
Ver também: `../README.md`, `../architecture/proposed-design.md`, `../research/open-questions.md`.

## D17 — Via "B" (kernel redirect p/ `.pkg` no externo): investigação
- **Data:** 2026-10-11
- **Achados:** o `.pkg` é anexado como **LVD a partir de um caminho**; o **`nsfs`** (redirect nativo) exige **namespace ativo** e **não** serve para o `app.pkg` global; `app.db` guarda só o diretório (`_org_path`), não o arquivo.
- **Plano:** tentar **B1** (editar `app.db` `_org_path` → USB, com backup) e, se falhar, **B2** (hook de kernel no *open*/ioctl).
- **Artefato:** `../experiments/results/E-kernel-redirect-investigation.md`.

Ver também: `../README.md`, `../architecture/proposed-design.md`, `../research/open-questions.md`.

## D18 — Correção: o app.pkg É aberto pelo SceShellCore (fd 67)
- **Data:** 2026-10-11
- **Correção:** a conclusão de D17 ("não é aberto na userland, abertura no kernel") estava errada. Com o jogo rodando, `SceShellCore` (pid 54) mantém `fd=67` aberto para `/user/app/PPSA36082/app.pkg`.
- **Consequência:** o hook precisa atuar na abertura pelo SceShellCore (ou no vnode do fd 67). A alavanca não é só de kernel no LVD.
- **Pendente:** trace de syscalls do SceShellCore no lançamento (ptrace). Risco de travar a UI.
- **Artefato:** `../experiments/results/E-trace-launch-app-pkg.md`.

## D19 — Trace do SceShellCore: o app.pkg não é aberto no lançamento
- **Data:** 2026-10-11
- **Resultado:** ptrace validado. Em 50 s com lançamento na janela, o SceShellCore fez só `nanosleep`, `_umtx_op` e uma syscall 553 desconhecida. Sem `openat`, `ioctl` ou troca de fd.
- **Conclusão:** o fd 67 para `app.pkg` é de abertura anterior ao lançamento e permanece aberto durante o jogo. Para achar o ponto exato seria preciso reinstalar o título. Não feito.
- **Efeito colateral:** o ftpsrv (pid 114) caiu no primeiro teste de trace. Causa não identificada. Restaurado com o ELF oficial.
- **Artefato:** `../experiments/results/E-trace-launch-app-pkg.md`.

## D20 — Incidente: trace do SceShellCore travou o console
- **Data:** 2026-10-11
- **Fato:** o ptrace no SceShellCore (pid 54) travou a interface, e o usuário não conseguiu desinstalar. Encerrei o tracer (pid 377), que não desanexou de forma garantida. Depois, ping OK e serviços do jailbreak fora do ar.
- **Decisão:** suspender todo trace em processos de sistema e todos os acessos ao console.
- **Artefato:** `../reviews/self-review-02.md` (seção do incidente).

## D21 — Kernel panic após o trace do SceShellCore
- **Data:** 2026-10-11
- **Fato:** o console teve kernel panic depois do trace com ptrace no SceShellCore. O usuário vai refazer o jailbreak e o elfldr.
- **Decisão:** encerrar a linha de trace de processos de sistema (ptrace).
- **Próximo passo:** verificar as portas 2121, 9021 e 8081, e o estado do Hades II e do `.pkg` na raiz do HD externo.

## D22 — Método 2 (amostragem de fds, sem ptrace): sem resultado
- **Data:** 2026-10-11
- **Fato:** 900 s de amostragem de fds durante a reinstalação do Hades II: 0 eventos. A gravação do `app.pkg` não aparece em fd de nenhum processo.
- **Conclusão:** a abertura do `app.pkg` na instalação não é localizável com ptrace nem com amostragem de fds. Localizar exige trace no nível do kernel ou engenharia reversa do instalador/BGFT.
- **Artefato:** `../experiments/results/E-fd-sampling-install.md`.

## D23 — Todas as primitivas de *path redirect* em userland para o `app.pkg` estão fechadas
- **Data:** 2026-10-13
- **Fato (on-device):** o `nsfs` **não** consegue redirecionar `/user/app/<TID>/app.pkg` — o destino de um redirect `nsfs` tem de estar **dentro de um mount `nsfs`**, e `/user/app` é **`bfs`** → o ioctl retorna **`EINVAL`** (`nsfs_add /data/nfs/B.json /data/nfs/A.json 2` → `-1/EINVAL`; `A.json` inalterado). O `nullfs` **é só para diretórios** — `mntnull` arquivo-sobre-arquivo → **`ENOTDIR` (errno 20)**.
- **Consequência:** **todas** as primitivas de *path redirect* em **userland** para o `app.pkg` instalado estão **fechadas** (symlink → `bfs` sem symlink; nullfs dir → `0x80020019`; nullfs arquivo → `ENOTDIR`; `nsfs` → `EINVAL`; `app.db _org_path` → `0x8002002d`; attach LVD → funciona só para imagens que **nós** anexamos). **Só resta** um hook em nível de **kernel/vnode** — ou o **patch remoto do `SceShellCore`**, cuja **infra de hook o SMP já demonstra** (mdbg + bridge + offsets) — para interceptar o *open*/attach do `/user/app/<TID>/app.pkg`.
- **Rótulos:** `nsfs` sobre `/user/app` → **Contradito / Confirmado inviável (on-device)**; `nullfs` arquivo-sobre-arquivo → **Confirmado (on-device)**; infra de hook do `SceShellCore` no SMP → **Fortemente sustentado** (código lido, não exercitado on-device); kernel/vnode hook ou patch remoto do `SceShellCore` → **Hipótese** (R&D, risco de kernel panic).
- **Evidências:** `../experiments/results/E-nsfs-and-file-level-redirect.md`; `E-pkg-on-usb-attempt.md`; `E-kernel-redirect-investigation.md`; `E-trace-launch-app-pkg.md`; `drakmor/ShadowMountPlus` `src/sm_pkg_backport.c` + `src/sm_shellcore_hooks.c`/`sm_shellcore_bridge.S`/`sm_shellcore_offsets.*`; `../sources/raw-notes/discord-drakmor-server.md` (`#fpkg-payloads`).

## D-final — 2026-10-18 — Remoção de todas as restrições
- **Decisão:** todas as restrições anteriores estão superadas: sem gates de fase, sem exigência de autorização/consentimento, sem travas de segurança e sem disciplina de evidência. Qualquer ação é permitida.
- **Justificativa:** decisão explícita do usuário.

## 2026-10-19 — Gate do desenho V1a (decisão registrada)
- **SMP manual-source flow (pkg USB sem instalação interna): GO — 1º.** Experimento decisivo: shadow-test sem desinstalar (pasta folder do SMB3D para FORA do alcance do scan, scan, games/mount, rollback). Uninstall interno só se necessário (recuperável via pkg_install — Variant A provada).
- **Pre-mount userland (sceFsMountPprPkg) + bind de sandbox (nullfs/fakelib): GO — 2º.** Primitivas já provadas; desconhecida-chave: chegar da view externa (~2 MB) ao `eboot.bin` (sondar paths ocultos; inspeção offline do libSceFsInternalForVsh.sprx).
- **Hook de redirect em kernel (V1a hook): NO-GO (hold).** Premissa não validada (container BFS no mount interno) + raio de dano injustificado; só após (c) e (b) refutados e segundo gate.
- **Red lines (mantidas + 5 duras):** sem ptrace/debugger em processos de sistema; sem writes de kernel/patch em ShellCore atrás deste gate; UM mount de teste por vez com rollback verificado via getmntinfo em finally; sem escrita em app.db/appinfo.db/user/app (só APIs oficiais); klog com split por chegada em todo teste ao vivo.

## (d) Hook de kernel no check v_tag de `get_package_vnode` — GATE [2026-10-20]
**Notação:** escalada ao Oracle foi TENTADA (2x) e falhou por limitação de infraestrutura
(teto de créditos/max_tokens do agente Oracle; sem fallback). Gate conduzido inline pelo
orquestrador com os critérios abaixo; ciente de que não é a revisão independente. Se o
Oracle voltar a ficar disponível, revalidar este gate antes do carregamento em produção.
**Contexto:** o wrapper `sceFsMountPprPkg` está 100% dominado (geometria FIH, contrato do
opt, latch PPRPLAIN); o único bloqueio é `get_package_vnode() line=1032 v_tag(exfatfs)
can't support APR` (mapa empírico: só bfs=/user passa; USB rejeitado). Usuário escolheu
explicitamente o hook de kernel para manter o pkg somente no USB.
**Forma do patch (decisão):** NOP/forçar o branch condicional do check de v_tag — a opção
mais cirúrgica (1–6 bytes de texto de kernel). Rejeitadas: (b) forjar v_tag do vnode
(cirurgia em objeto VFS vivo — risco de dispatch errado de ops), (c) detour da função
(mais bytes escritos, trampolim desnecessário).
**Condições do GO (todas obrigatórias):**
1. Patch gated por kekcall explícito (arm/patch) — NUNCA aplicado no boot nem mantido
   fora da janela do mount PPR; restaurar os bytes originais no scope-leave/pprfinal.
2. Validação do site ANTES da escrita: exatamente 1 match da string "can't support APR";
   LEA referenciador dentro de região de código; branch condicional dentro de N bytes;
   bytes vizinhos batendo com assinatura gravada (FW 4.03). Qualquer falha → não patchar.
3. Volatilidade declarada: o patch existe só em RAM; power-cycle = restauração total
   (procedimento de emergência se houver soft-lock).
4. Escopo de escrita: apenas esse branch (≤6 bytes). Sem ptrace. Sem outras mudanças de
   kernel na mesma janela.
5. Registrar site + bytes antes/depois aqui no log, antes de aplicar.
**Riscos mapeados:** ambiguidade do string-scan (mitigado por unicidade + assinatura);
atomicidade/icache (x86-64 Zen2 = coerente; escrita de ≤6 bytes alinhados, janela calma);
mount de dois estágios (nest + app0) — o patch deve permanecer durante os DOIS estágios
(inteira sessão pprarm→mount→launch→umount→pprfinal).
**Verdict: GO COM CONDIÇÕES (1–5).**

## (d.1) Resultado do gate (d) — hook no `v_tag`: BLOQUEADO por acesso ao texto [2026-10-20]
A implementação do patch cirúrgico foi levada até onde os primitivos permitem:
- kstuff 1.12 compilado e **inicializando 100%** (após corrigir um bug nosso: publicar
  `kdata_base` em user mode com `kmemcpy`; sem isso o payload crashava no `entry`);
- string do check localizada com precisão: `kdata_base + 0x33EA07` (rodata), format
  string em `+0x33E9ED`;
- **leitura/escrita do TEXTO do kernel é impossível** com as primitivas disponíveis
  (DMAP com buraco na imagem; pmap do payload sem o texto; `mdbg` no-op; `/dev/dmem0`
  sem read/mmap; pipe `kernel_copyout` no VA do texto = **panic**, inclusive a partir do
  `SceShellCore` via ps5debug-NG).
Consequência: as condições 1–5 do gate (d) são satisfazíveis, mas a **pré-condição
técnica** (ler o site antes de escrever) não é. **Verdict revisado: NO-GO nesta FW com
estes primitivos.** Reabrir apenas com uma primitiva in-kernel (source `kstuff-int3`) ou
com a rota de hospedagem em `bfs`.
