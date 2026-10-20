/*
 * p1_tool.c — P1 probe payload. COPY of /tmp/opencode/diag/tool.c, rewritten
 * for the P1 experiment per the parent review (p1-review-verdict.md §4).
 * The original tool.c / tool3.elf are NOT modified.
 *
 * Review fix (verdict §2): the old "simplified" lvd_layer struct and
 * _IOW('l',1,…)/_IO('l',2) encodings were WRONG for the real SCE_LVD_IOC_*.
 * This copy uses the REAL structs/constants (see lvd_types.h; primary source
 * kstuff-lite ps5-kstuff-ldr/include/types.h, verified at p1-review-verdict.md:9):
 *   SCE_LVD_IOC_ATTACH 0xC0286D00 (lvd_ioctl_attach_t with layers_ptr)
 *   SCE_LVD_IOC_DETACH 0xC0286D01 (lvd_ioctl_detach_t, by device_id)
 * Attach prints the returned device_id — that is the /dev/lvdN to use as
 * nmount "from" (closes the from-gap, verdict §3).
 *
 * Commands:
 *   mkdir <path>                                      — probe mountpoint
 *   rmdir <path>                                      — scratch cleanup;
 *         leaf with '*'/'?' matches via p1_glob_match (readdir parent +
 *         rmdir(2) each match, empty dirs only); exact names -> rmdir(2)
 *   lvdattach <path> <sector> <img_type> [option_len] — real LVD attach;
 *         layer{source_type=1, entry_flags=1, path, offset=0, size=stat},
 *         attach{io_version=0, device_id=-1, sector_size_0/1=<sector>,
 *         option_len=<0x1C default>, image_type, layer_count=1,
 *         device_size=layer.size, layers_ptr=&layer}   (pfsc_mount.c profile)
 *   lvddetach <device_id>                             — real LVD detach
 *   mntfs <fstype> <source> <mountpoint> [opt...]     — nmount iovec builder
 *   umount <path> [force]                             — unmount(2) wrapper
 *   ls <path> / statfs <path> / nsfs_add …            — unchanged reads
 *
 * Tokenizer/kv/u32/i32 helpers live in parse.c (host-tested via test_parse.c);
 * LVD layout is locked by the same test binary.
 *
 * Build (offline, NOT deployed):
 *   PS5_PAYLOAD_SDK=/tmp/opencode/sdk/prebuilt/ps5-payload-sdk make
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ps5/kernel.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <dirent.h>

#include "parse.h"
#include "lvd_types.h"
#include <dlfcn.h>

static char *tok[256];
static int ntok;

/* ---- kstuff PPRPLAIN kekcall protocol (EchoStretch/kstuff-lite) ----------
 * uelf/kekcall.c nr==7 -> control_ppr_plaintext_request(magic, mode, arg0,
 * unused=0, arg2, arg3, *result); userspace pattern from ps5-kstuff/debug-reader.c:
 * syscall with rax = 0x0000000700000027 (nr 7 over the hijacked syscall 0x27).
 * Modes: SCOPE_ENTER 11, BEGIN 1, WRITE 2, CHECK 3, PLAINTEXT_NOAUTH 9,
 * finalize 0, SCOPE_LEAVE 12. Protocol version = 8. Staging = FIH 0x1000 +
 * sblock 0x5a0 (16-byte WRITE chunks, sequential offsets). */
#define PPR_MAGIC   0x505052504c41494eull
#define PPR_VER     8
#define PPR_BEGIN   1
#define PPR_WRITE   2
#define PPR_CHECK   3
#define PPR_NOAUTH  9
#define PPR_FINISH  0
#define PPR_ENTER   11
#define PPR_LEAVE   12
#define KSTUFF_SCOPE_PPR_MOUNT 3
#define KEKCALL_PPR 0x0000000700000027ull

static int64_t kekcall(uint64_t a, uint64_t b, uint64_t c,
                       uint64_t d, uint64_t e, uint64_t f, uint64_t g)
{
    register uint64_t r10 __asm__("r10") = d;
    register uint64_t r8 __asm__("r8") = e;
    register uint64_t r9 __asm__("r9") = f;
    uint64_t ret;
    unsigned char is_error;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret), "=@ccc"(is_error), "+r"(r10), "+r"(r8), "+r"(r9)
        : "a"(g), "D"(a), "S"(b), "d"(c)
        : "rcx", "r11", "memory");
    return is_error ? -(int64_t)ret : (int64_t)ret;
}

/* pprarm <pkg_path>: enter scope, stream the FIH+sblock staging, check, arm
 * the plaintext latch. Leaves the scope OPEN for the subsequent mount. */
static void cmd_pprarm(void) {
    uint8_t staging[0x15a0];
    uint64_t sblock_off;
    int fd, i;
    int64_t r;

    if (ntok < 2) { printf("ERR usage: pprarm <pkg_path>\nOK\n"); return; }
    fd = open(tok[1], O_RDONLY);
    if (fd < 0) { printf("ERR open: %s\nOK\n", strerror(errno)); return; }
    if (read(fd, staging, 0x1000) != 0x1000) {
        close(fd); printf("ERR read FIH\nOK\n"); return;
    }
    memcpy(&sblock_off, staging + 0x20, 8); /* FIH field: sblock offset (LE) */
    if (sblock_off == 0 || sblock_off > 0x7fffffffULL) {
        close(fd); printf("ERR bad sblock offset %#lx\nOK\n", (unsigned long)sblock_off); return;
    }
    if (lseek(fd, (off_t)sblock_off, SEEK_SET) < 0 ||
        read(fd, staging + 0x1000, 0x5a0) != 0x5a0) {
        close(fd); printf("ERR read sblock @%#lx\nOK\n", (unsigned long)sblock_off); return;
    }
    close(fd);

    r = kekcall(PPR_MAGIC, PPR_ENTER, PPR_VER, 0, KSTUFF_SCOPE_PPR_MOUNT, 0, KEKCALL_PPR);
    printf("pprarm scope_enter ret=%ld\n", (long)r);
    if (r) { printf("ERR scope_enter\nOK\n"); return; }
    r = kekcall(PPR_MAGIC, PPR_BEGIN, PPR_VER, 0, 0, 0, KEKCALL_PPR);
    printf("pprarm begin ret=%ld\n", (long)r);
    if (r) { printf("ERR begin\nOK\n"); return; }
    for (i = 0; i < 0x15a0; i += 16) {
        uint64_t w1, w2;
        memcpy(&w1, staging + i, 8);
        memcpy(&w2, staging + i + 8, 8);
        r = kekcall(PPR_MAGIC, PPR_WRITE, (uint64_t)i, 0, w1, w2, KEKCALL_PPR);
        if (r) { printf("ERR write @%#x ret=%ld\nOK\n", i, (long)r); return; }
    }
    r = kekcall(PPR_MAGIC, PPR_CHECK, 0, 0, 0, 0, KEKCALL_PPR);
    printf("pprarm check ret=%ld (0=staging OK)\n", (long)r);
    r = kekcall(PPR_MAGIC, PPR_NOAUTH, PPR_VER, 0, 0, 0, KEKCALL_PPR);
    printf("pprarm noauth ret=%ld (0=latch armed)\n", (long)r);
    printf("OK\n");
}

/* pprfinal <ret>: report the mount result and leave the scope. */
static void cmd_pprfinal(void) {
    int64_t r;
    int ret = 0;
    if (ntok >= 2) p1_parse_i32(tok[1], &ret);
    r = kekcall(PPR_MAGIC, PPR_FINISH, PPR_VER, 0, (uint64_t)(uint32_t)ret, 0, KEKCALL_PPR);
    printf("pprfinal finish ret=%ld\n", (long)r);
    r = kekcall(PPR_MAGIC, PPR_LEAVE, PPR_VER, 0, KSTUFF_SCOPE_PPR_MOUNT, 0, KEKCALL_PPR);
    printf("pprfinal scope_leave ret=%ld\n", (long)r);
    printf("OK\n");
}

static void *load_fs_internal(void);

/* mountpkg2 <pkg> <outer_mnt> <inner_mnt> — the full two-stage contract
 * (RE findings2.md): sceFsMountPprPkg performs BOTH mounts internally:
 *   +0x00 outer image file (pkg)
 *   +0x08 inner mount path   (where eboot.bin appears)
 *   +0x10 budgetid string ptr ("game")
 *   +0x18 volume start offset (FIH+0x10)
 *   +0x20 sblock file offset  (FIH+0x20)
 *   +0x30 inner sblock offset (FIH+0x50 << 16 — the NAPS logical space)
 *   +0x38 mode 0x101 (game)
 *   +0x80 outer mount path
 *   +0x88 inner image path   (<outer_mnt>/pfs_image.dat)
 * arg2 = {handle_out} buffer. */
static void cmd_mountpkg2(void) {
    void *h;
    int (*init_opt)(void *);
    int (*mount_pkg)(void *, const char *, const void *, int, int, int);
    uint8_t optbuf[512];
    uint8_t key[64];
    static char innerimg[256];
    uint8_t pathbuf[256];
    uint64_t fih[16];
    int fd;
    uint64_t vol_start, sblock_abs, inner_sblk;
    uint32_t mode = 0x101;
    int ret;

    if (ntok < 4) { printf("ERR usage: mountpkg2 <pkg> <outer_mnt> <inner_mnt> [vol_start]\nOK\n"); return; }
    fd = open(tok[1], O_RDONLY);
    if (fd < 0) { printf("ERR open pkg: %s\nOK\n", strerror(errno)); return; }
    {   /* devices (lvd) need 512-byte-aligned reads */
        uint8_t tmp[512];
        ssize_t nr = read(fd, tmp, sizeof(tmp));
        if (nr < (ssize_t)sizeof(fih)) { close(fd); printf("ERR read FIH (%zd)\nOK\n", nr); return; }
        memcpy(fih, tmp, sizeof(fih));
    }
    close(fd);
    vol_start = fih[2];        /* FIH+0x10 */
    if (ntok >= 5) {           /* override: device nodes have st_size=0; 0 = whole */
        uint64_t vs = 0;
        vs = strtoull(tok[4], NULL, 0); vol_start = vs;
    }
    sblock_abs = fih[4];       /* FIH+0x20 */
    inner_sblk = fih[10] << 16; /* FIH+0x50 (block index) << 16 = NAPS logical offset */

    h = load_fs_internal();
    if (!h) { printf("ERR dlopen\nOK\n"); return; }
    init_opt = (int (*)(void *))dlsym(h, "sceFsInitMountPprPkgOpt");
    mount_pkg = (int (*)(void *, const char *, const void *, int, int, int))
                dlsym(h, "sceFsMountPprPkg");
    if (!init_opt || !mount_pkg) { printf("ERR dlsym\nOK\n"); return; }

    memset(optbuf, 0, sizeof(optbuf));
    if (init_opt(optbuf) < 0) { printf("ERR init\nOK\n"); return; }
    snprintf(innerimg, sizeof(innerimg), "%s/pfs_image.dat", tok[2]);
    memset(key, 0, sizeof(key));
    memset(pathbuf, 0, sizeof(pathbuf));

    *(const char **)optbuf = tok[1];               /* +0x00 outer file   */
    *(char **)(optbuf + 0x08) = tok[3];            /* +0x08 inner mnt    */
    *(char **)(optbuf + 0x10) = "game";            /* +0x10 budgetid     */
    *(uint64_t *)(optbuf + 0x18) = vol_start;      /* +0x18 volume start */
    *(uint64_t *)(optbuf + 0x20) = sblock_abs;     /* +0x20 sblock file  */
    *(uint64_t *)(optbuf + 0x30) = inner_sblk;     /* +0x30 inner sblock */
    *(uint32_t *)(optbuf + 0x38) = mode;           /* +0x38 mode         */
    *(const char **)(optbuf + 0x80) = tok[2];      /* +0x80 outer mnt    */
    *(const char **)(optbuf + 0x88) = innerimg;    /* +0x88 inner image  */

    printf("GEOM vol=%#lx sblock=%#lx inner=%#lx\n",
           (unsigned long)vol_start, (unsigned long)sblock_abs, (unsigned long)inner_sblk);
    ret = mount_pkg(optbuf, pathbuf, key, 0, 0, 0);
    {
        int32_t handle;
        memcpy(&handle, pathbuf, 4);
        printf("HANDLE %d\n", (int)handle);
    }
    if (ret < 0) printf("ERR sceFsMountPprPkg ret=0x%x\n", (unsigned)ret);
    else printf("OK sceFsMountPprPkg %s -> %s + %s\n", tok[1], tok[2], tok[3]);
    printf("OK\n");
}

#ifndef KEKCALL_KREAD
#define KEKCALL_KREAD 0x0000000800000027ull
#define KEKCALL_KDATA 0x0000000900000027ull
#endif

/* ---- kernel-context reads via the instrumented kstuff (kekcall 8/9) ---- */
static void cmd_kkread(void) {
    static uint8_t b[4096]; uint64_t va; unsigned long n; int64_t r;
    if (ntok < 3) { printf("ERR usage: kkread <kva_hex> <len>\nOK\n"); return; }
    va=strtoull(tok[1],NULL,0); n=strtoul(tok[2],NULL,0);
    if (!n || n>sizeof(b)) { printf("ERR len 1..4096\nOK\n"); return; }
    r=kekcall((uint64_t)(uintptr_t)b, va, n, 0,0,0, KEKCALL_KREAD);
    if (r != (int64_t)n) { printf("ERR kkread r=%ld\nOK\n",(long)r); return; }
    for (unsigned long i=0;i<n;i+=16) {
        printf("%016lx:",(unsigned long)(va+i));
        for (unsigned long j=0;j<16 && i+j<n;j++) printf(" %02x", b[i+j]);
        printf("  ");
        for (unsigned long j=0;j<16 && i+j<n;j++){unsigned char c=b[i+j];printf("%c",(c>=32&&c<127)?c:'.');}
        printf("\n");
    }
    printf("OK\n");
}

static void cmd_kkxref(void) {
    static uint8_t page[4096];
    static const char needle[] = "can't support APR";
    int64_t kb = kekcall(0,0,0,0,0,0,KEKCALL_KDATA);
    uint64_t text, found=0, target; int i, ok=0, fails=0, refs=0;
    if (kb <= 0) { printf("ERR kdata base unavailable (%ld)\nOK\n",(long)kb); return; }
    text=(uint64_t)kb - 0xC00000ull;
    printf("KDATA=%#lx TEXT=%#lx\n",(unsigned long)kb,(unsigned long)text);
    for (uint64_t va=text; va<(uint64_t)kb+0x4000000ull && !found; va+=0x1000) {
        if (kekcall((uint64_t)(uintptr_t)page, va, 0x1000, 0,0,0, KEKCALL_KREAD) != 0x1000) { fails++; continue; }
        ok++;
        for (i=0; i+(int)sizeof(needle)<=0x1000; i++) {
            if (page[i]!=(unsigned char)needle[0]) continue;
            if (memcmp(page+i,needle,sizeof(needle)-1)) continue;
            found=va+i; break;
        }
    }
    printf("SCAN ok=%d fails=%d found=%#lx\n", ok, fails, (unsigned long)found);
    if (!found) { printf("OK\n"); return; }
    target=found-26;
    if (kekcall((uint64_t)(uintptr_t)page, target, 32, 0,0,0, KEKCALL_KREAD)==32) {
        char t[40]; int n=0;
        while (n<32 && page[n]>=32 && page[n]<127) { t[n]=(char)page[n]; n++; }
        t[n]=0;
        printf("TARGET=%#lx str='%s'\n",(unsigned long)target,t);
    }
    for (uint64_t va=text; va<(uint64_t)kb; va+=0x1000) {
        if (kekcall((uint64_t)(uintptr_t)page, va, 0x1000, 0,0,0, KEKCALL_KREAD) != 0x1000) continue;
        for (i=0; i+7<0x1000; i++) {
            unsigned char b0=page[i], b1=page[i+1], b2=page[i+2];
            if ((b0==0x48||b0==0x49||b0==0x4c||b0==0x4d) && b1==0x8d && (b2&0xC7)==0x05) {
                int32_t d; memcpy(&d,page+i+3,4);
                uint64_t t=va+i+7+(int64_t)d;
                if (t==target) { printf("XREF lea %#lx\n",(unsigned long)(va+i)); refs++; }
            } else if ((b0==0x48||b0==0x49) && b1>=0xb8 && b1<=0xbf) {
                uint64_t imm; memcpy(&imm,page+i+2,8);
                if (imm==target) { printf("XREF movabs %#lx\n",(unsigned long)(va+i)); refs++; }
            }
        }
    }
    printf("REFS=%d\nOK\n", refs);
}

/* ---- SDK kernel primitives: scan/patch the kernel's v_tag APR check ---- */
/* Safe kernel access: walk page tables (kernel_proc_getpaddr) and read the
 * physical page through the DMAP, which is always mapped. Invalid pages fail
 * gracefully instead of faulting the pipe primitive. */
extern unsigned long KERNEL_ADDRESS_DMAP_BASE;
int kernel_proc_getpaddr(int pid, unsigned long vaddr,
                         unsigned long *paddr, unsigned long *plen);
static pid_t kpid = -2;
static int kread_safe(uint64_t va, void *buf, size_t len) {
    size_t done = 0;
    if (kpid == -2) kpid = getpid();
    while (done < len) {
        unsigned long paddr = 0, plen = 0;
        size_t n;
        if (kernel_proc_getpaddr(kpid, (unsigned long)(va + done), &paddr, &plen))
            return -1;
        n = plen; if (n > len - done) n = len - done;
        if (kernel_copyout(KERNEL_ADDRESS_DMAP_BASE + paddr, (char *)buf + done, n))
            return -1;
        done += n;
    }
    return 0;
}
static int kwrite_safe(uint64_t va, const void *buf, size_t len) {
    size_t done = 0;
    if (kpid == -2) kpid = getpid();
    while (done < len) {
        unsigned long paddr = 0, plen = 0;
        size_t n;
        if (kernel_proc_getpaddr(kpid, (unsigned long)(va + done), &paddr, &plen))
            return -1;
        n = plen; if (n > len - done) n = len - done;
        if (kernel_copyin((const char *)buf + done, KERNEL_ADDRESS_DMAP_BASE + paddr, n))
            return -1;
        done += n;
    }
    return 0;
}

extern int mdbg_copyout(int pid, unsigned long addr, void *buf, unsigned long len);
static void cmd_kmdbg(void) {
    static uint8_t buf[256];
    int pid; uint64_t va; unsigned long n; int r;
    if (ntok < 4) { printf("ERR usage: kmdbg <pid> <kva_hex> <len>\nOK\n"); return; }
    pid=(int)strtol(tok[1],NULL,0); va=strtoull(tok[2],NULL,0); n=strtoul(tok[3],NULL,0);
    if (!n || n>sizeof(buf)) { printf("ERR len 1..256\nOK\n"); return; }
    memset(buf,0xAA,sizeof(buf));
    r=mdbg_copyout(pid, (unsigned long)va, buf, n);
    printf("KMDBG pid=%d va=%#lx r=%d\n", pid, (unsigned long)va, r);
    for (unsigned long i=0;i<n;i+=16) {
        printf("%09lx:",(unsigned long)(va+i));
        for (unsigned long j=0;j<16 && i+j<n;j++) printf(" %02x", buf[i+j]);
        printf("  ");
        for (unsigned long j=0;j<16 && i+j<n;j++){unsigned char c=buf[i+j];printf("%c",(c>=32&&c<127)?c:'.');}
        printf("\n");
    }
    printf("OK\n");
}

static void cmd_kwalk(void) {
    uint64_t va; unsigned long paddr=0, plen=0; int r; int pid;
    if (ntok < 2) { printf("ERR usage: kwalk <kva_hex> [pid]\nOK\n"); return; }
    va=strtoull(tok[1],NULL,0);
    if (kpid == -2) kpid = getpid();
    pid = (ntok >= 3) ? (int)strtol(tok[2],NULL,0) : (int)kpid;
    r=kernel_proc_getpaddr(pid, (unsigned long)va, &paddr, &plen);
    printf("KWALK va=%#lx pid=%d r=%d paddr=%#lx plen=%#lx\n", (unsigned long)va, pid, r,
           (unsigned long)paddr, (unsigned long)plen);
    printf("OK\n");
}
static void cmd_kgetlong(void) {
    uint64_t va; uint64_t v;
    if (ntok < 2) { printf("ERR usage: kgetlong <kva_hex>\nOK\n"); return; }
    va=strtoull(tok[1],NULL,0);
    v=kernel_getlong(va);
    printf("KLONG %#lx = %#lx\n", (unsigned long)va, (unsigned long)v);
    printf("OK\n");
}

static void cmd_kmemtest(void) {
    static uint8_t b[32];
    printf("DMAP_BASE=%#lx pid=%d\n", (unsigned long)KERNEL_ADDRESS_DMAP_BASE, (int)getpid());
    for (uint64_t va = (uint64_t)KERNEL_ADDRESS_TEXT_BASE; va < (uint64_t)KERNEL_ADDRESS_DATA_BASE; va += 0x200000) {
        int r = kread_safe(va, b, sizeof(b));
        printf("%#lx r=%d:", (unsigned long)va, r);
        if (!r) { for (int i=0;i<16;i++) printf(" %02x", b[i]); }
        printf("\n");
    }
    printf("OK\n");
}

/* kxref <target_va> [start] [end]: find RIP-relative LEA/movabs refs to a
 * kernel VA inside the kernel text. Read-only. */
static void cmd_kxref(void) {
    static uint8_t page[4096];
    uint64_t target, start, end;
    int n=0, pages=0, fails=0;
    if (ntok < 2) { printf("ERR usage: kxref <target_hex> [start] [end]\nOK\n"); return; }
    target=strtoull(tok[1],NULL,0);
    start = ntok>=3 ? strtoull(tok[2],NULL,0) : (uint64_t)KERNEL_ADDRESS_TEXT_BASE;
    end   = ntok>=4 ? strtoull(tok[3],NULL,0) : (uint64_t)KERNEL_ADDRESS_DATA_BASE;
    printf("KXREF target=%#lx range=[%#lx,%#lx)\n",(unsigned long)target,(unsigned long)start,(unsigned long)end);
    for (uint64_t va=start; va<end; va+=0x1000) {
        if (kread_safe(va, page, 0x1000)) { fails++; continue; }
        pages++;
        for (int i=0;i<0x1000-7;i++) {
            uint8_t b0=page[i], b1=page[i+1], b2=page[i+2];
            if ((b0==0x48||b0==0x49||b0==0x4c||b0==0x4d) && b1==0x8d && (b2&0xC7)==0x05) {
                int32_t d; memcpy(&d, page+i+3, 4);
                uint64_t t = va + i + 7 + (int64_t)d;
                if (t == target) { printf("XREF lea %#lx\n",(unsigned long)(va+i)); n++; }
            } else if (b0==0x8d && (b1&0xC7)==0x05) {
                int32_t d; memcpy(&d, page+i+2, 4);
                uint64_t t = va + i + 6 + (int64_t)d;
                if (t == target) { printf("XREF lea32 %#lx\n",(unsigned long)(va+i)); n++; }
            } else if ((b0==0x48||b0==0x49) && b1>=0xb8 && b1<=0xbf) {
                uint64_t imm; memcpy(&imm, page+i+2, 8);
                if (imm == target) { printf("XREF movabs %#lx\n",(unsigned long)(va+i)); n++; }
            }
        }
    }
    printf("KXREF pages=%d fails=%d refs=%d\nOK\n",pages,fails,n);
}

/* Physical-memory access through the DMAP (always mapped, no page walk). */
static int kphys_read(uint64_t paddr, void *buf, size_t len) {
    return kernel_copyout(KERNEL_ADDRESS_DMAP_BASE + paddr, buf, len) ? -1 : 0;
}
static void cmd_dmmap(void) {
    uint64_t pa; unsigned long n; int fd; void *m; uint8_t *b;
    if (ntok < 3) { printf("ERR usage: dmmap <paddr_hex> <len>\nOK\n"); return; }
    pa=strtoull(tok[1],NULL,0); n=strtoul(tok[2],NULL,0);
    if (!n || n>4096) { printf("ERR len 1..4096\nOK\n"); return; }
    fd=open("/dev/dmem0", O_RDONLY);
    if (fd<0) { printf("ERR open dmem0: %s\nOK\n", strerror(errno)); return; }
    m=mmap(NULL, n, PROT_READ, MAP_SHARED, fd, (off_t)(pa & ~0xfffull));
    close(fd);
    if (m==MAP_FAILED) { printf("ERR mmap: %s\nOK\n", strerror(errno)); return; }
    b=(uint8_t*)m + (pa & 0xfff);
    printf("DMMAP pa=%#lx\n",(unsigned long)pa);
    for (unsigned long i=0;i<n;i+=16) {
        printf("%09lx:",(unsigned long)(pa+i));
        for (unsigned long j=0;j<16 && i+j<n;j++) printf(" %02x", b[i+j]);
        printf("  ");
        for (unsigned long j=0;j<16 && i+j<n;j++){unsigned char c=b[i+j];printf("%c",(c>=32&&c<127)?c:'.');}
        printf("\n");
    }
    munmap(m, n);
    printf("OK\n");
}

static void cmd_dmem(void) {
    static uint8_t buf[4096];
    uint64_t pa; unsigned long n; int fd; ssize_t r;
    if (ntok < 3) { printf("ERR usage: dmem <paddr_hex> <len>\nOK\n"); return; }
    pa=strtoull(tok[1],NULL,0); n=strtoul(tok[2],NULL,0);
    if (!n || n>sizeof(buf)) { printf("ERR len 1..4096\nOK\n"); return; }
    fd=open("/dev/dmem0", O_RDONLY);
    if (fd<0) { printf("ERR open dmem0: %s\nOK\n", strerror(errno)); return; }
    r=pread(fd, buf, n, (off_t)pa);
    if (r<0) { printf("ERR pread: %s\nOK\n", strerror(errno)); close(fd); return; }
    close(fd);
    printf("DMEM pa=%#lx got=%zd\n",(unsigned long)pa,r);
    for (unsigned long i=0;i<(unsigned long)r;i+=16) {
        printf("%09lx:",(unsigned long)(pa+i));
        for (unsigned long j=0;j<16 && i+j<(unsigned long)r;j++) printf(" %02x", buf[i+j]);
        printf("  ");
        for (unsigned long j=0;j<16 && i+j<(unsigned long)r;j++){unsigned char c=buf[i+j];printf("%c",(c>=32&&c<127)?c:'.');}
        printf("\n");
    }
    printf("OK\n");
}

static void cmd_kphys(void) {
    static uint8_t buf[4096];
    uint64_t pa; unsigned long n;
    if (ntok < 3) { printf("ERR usage: kphys <paddr_hex> <len>\nOK\n"); return; }
    pa=strtoull(tok[1],NULL,0); n=strtoul(tok[2],NULL,0);
    if (!n || n>sizeof(buf)) { printf("ERR len 1..4096\nOK\n"); return; }
    if (kphys_read(pa, buf, n)) { printf("ERR phys read failed\nOK\n"); return; }
    for (unsigned long i=0;i<n;i+=16) {
        printf("%09lx:",(unsigned long)(pa+i));
        for (unsigned long j=0;j<16 && i+j<n;j++) printf(" %02x", buf[i+j]);
        printf("  ");
        for (unsigned long j=0;j<16 && i+j<n;j++){unsigned char c=buf[i+j];printf("%c",(c>=32&&c<127)?c:'.');}
        printf("\n");
    }
    printf("OK\n");
}

/* kxref_p <target_va>: scan the kernel image PHYSICALLY (text_phys derived from
 * DATA_BASE paddr - 0xC00000) for RIP-relative refs to target_va. */
static void cmd_kxref_p(void) {
    static uint8_t page[4096];
    uint64_t target, dphys=0, tphys, start_phys, end_phys;
    unsigned long paddr=0, plen=0;
    int refs=0, pages=0, fails=0;
    if (ntok < 2) { printf("ERR usage: kxref_p <target_hex> [span_bytes]\nOK\n"); return; }
    target=strtoull(tok[1],NULL,0);
    if (kpid == -2) kpid = getpid();
    if (kernel_proc_getpaddr(kpid, (unsigned long)KERNEL_ADDRESS_DATA_BASE, &paddr, &plen)) {
        printf("ERR cannot resolve DATA_BASE paddr\nOK\n"); return;
    }
    dphys = paddr & ~0xfffull;
    tphys = dphys - 0x0C00000ull;
    start_phys = tphys;
    end_phys = tphys + (ntok>=3 ? strtoull(tok[2],NULL,0) : 0x0C00000ull);
    printf("KXREFP data_phys=%#lx text_phys=%#lx target=%#lx\n",(unsigned long)dphys,(unsigned long)tphys,(unsigned long)target);
    for (uint64_t pa=start_phys; pa<end_phys; pa+=0x1000) {
        if (kphys_read(pa, page, 0x1000)) { fails++; continue; }
        pages++;
        uint64_t va0 = (uint64_t)KERNEL_ADDRESS_TEXT_BASE + (pa - start_phys);
        for (int i=0;i<0x1000-7;i++) {
            uint8_t b0=page[i], b1=page[i+1], b2=page[i+2];
            if ((b0==0x48||b0==0x49||b0==0x4c||b0==0x4d) && b1==0x8d && (b2&0xC7)==0x05) {
                int32_t d; memcpy(&d, page+i+3, 4);
                uint64_t t = va0 + i + 7 + (int64_t)d;
                if (t == target) { printf("XREF lea va=%#lx pa=%#lx\n",(unsigned long)(va0+i),(unsigned long)(pa+i)); refs++; }
            } else if ((b0==0x48||b0==0x49) && b1>=0xb8 && b1<=0xbf) {
                uint64_t imm; memcpy(&imm, page+i+2, 8);
                if (imm == target) { printf("XREF movabs va=%#lx pa=%#lx\n",(unsigned long)(va0+i),(unsigned long)(pa+i)); refs++; }
            }
        }
    }
    printf("KXREFP pages=%d fails=%d refs=%d\nOK\n",pages,fails,refs);
}

static void cmd_kpeek(void) {
    uint64_t a; unsigned long n;
    static uint8_t buf[4096];
    if (ntok < 3) { printf("ERR usage: kpeek <kaddr_hex> <len>\nOK\n"); return; }
    a=strtoull(tok[1],NULL,0); n=strtoul(tok[2],NULL,0);
    if (!n || n>sizeof(buf)) { printf("ERR len 1..4096\nOK\n"); return; }
    if (kread_safe(a, buf, n)) { printf("ERR copyout failed errno=%d\nOK\n", errno); return; }
    for (unsigned long i=0;i<n;i+=16) {
        printf("%016lx:",(unsigned long)(a+i));
        for (unsigned long j=0;j<16 && i+j<n;j++) printf(" %02x", buf[i+j]);
        printf("  ");
        for (unsigned long j=0;j<16 && i+j<n;j++) { unsigned char c=buf[i+j]; printf("%c",(c>=32&&c<127)?c:'.'); }
        printf("\n");
    }
    printf("OK\n");
}

static void cmd_kscan_k(void) {
    static const char needle[] = "can't support APR";
    static uint8_t page[4096];
    uint64_t text = (uint64_t)KERNEL_ADDRESS_TEXT_BASE;
    uint64_t data = (uint64_t)KERNEL_ADDRESS_DATA_BASE;
    uint64_t start = text, end = data;
    int hits=0, okpages=0, failpages=0;
    if (!text || !data) { printf("ERR kernel base unavailable text=%lx data=%lx\nOK\n",(unsigned long)text,(unsigned long)data); return; }
    if (ntok >= 2) start = strtoull(tok[1],NULL,0);
    if (ntok >= 3) end = strtoull(tok[2],NULL,0);
    printf("KSCAN text=%#lx data=%#lx range=[%#lx,%#lx)\n",(unsigned long)text,(unsigned long)data,(unsigned long)start,(unsigned long)end);
    for (uint64_t va=start; va<end; va+=0x1000) {
        if (kread_safe(va, page, 0x1000)) { failpages++; continue; }
        if (((va - start) & 0x3ffffff) == 0) {
            printf("PROGRESS %#lx okpages=%d failpages=%d hits=%d\n",
                   (unsigned long)va, okpages, failpages, hits);
        }
        okpages++;
        for (int i=0;i+ (int)sizeof(needle) <= 0x1000;i++) {
            if (page[i]!=(uint8_t)needle[0]) continue;
            if (memcmp(page+i,needle,sizeof(needle)-1)) continue;
            printf("HIT %#lx : %s\n",(unsigned long)(va+i),(char*)page+i);
            if (i>=48) { printf("  pre : "); for (int j=i-48;j<i;j++){unsigned char c=page[j];printf("%c",(c>=32&&c<127)?c:'.');} printf("\n"); }
            { static uint8_t ctx[256];
              uint64_t cva=(va+i>=32)?(va+i-32):(va+i);
              if (!kread_safe(cva, ctx, sizeof(ctx))) {
                  printf("  ctxhex:"); for (int j=0;j<64;j++) printf(" %02x", ctx[j]); printf("\n");
                  printf("  ctxasc: "); for (int j=0;j<64;j++){unsigned char c=ctx[j];printf("%c",(c>=32&&c<127)?c:'.');} printf("\n");
              }
            }
            hits++;
        }
    }
    printf("KSCAN okpages=%d failpages=%d hits=%d\nOK\n", okpages, failpages, hits);
}

/* kpatch <kaddr> <expected_hex> <new_hex> — validates the current bytes, writes,
 * reads back and reports. Gate: caller must pass the exact expected bytes. */
static int hexval(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
static int hex2bin(const char* s, uint8_t* out, int maxn) {
    int n=0;
    while (s[0] && s[1]) { int a=hexval(s[0]), b=hexval(s[1]); if(a<0||b<0||n>=maxn) return -1; out[n++]=(uint8_t)((a<<4)|b); s+=2; }
    return s[0]? -1 : n;
}
static void cmd_kpatch(void) {
    static uint8_t cur[64], exp[64], nw[64], back[64];
    uint64_t a; int ne, nn;
    if (ntok < 4) { printf("ERR usage: kpatch <kaddr_hex> <expected_hex> <new_hex>\nOK\n"); return; }
    a=strtoull(tok[1],NULL,0);
    ne=hex2bin(tok[2],exp,sizeof(exp)); nn=hex2bin(tok[3],nw,sizeof(nw));
    if (ne<=0 || nn!=ne) { printf("ERR hex parse/len (expected %d new %d)\nOK\n", ne, nn); return; }
    if (kread_safe(a,cur,ne)) { printf("ERR read failed errno=%d\nOK\n", errno); return; }
    printf("CUR:"); for(int i=0;i<ne;i++) printf(" %02x",cur[i]); printf("\n");
    if (memcmp(cur,exp,ne)) { printf("REFUSE: current bytes != expected (no write)\nOK\n"); return; }
    if (kwrite_safe(a, nw, nn)) { printf("ERR write failed errno=%d\nOK\n", errno); return; }
    if (kread_safe(a,back,ne)) { printf("WARN: readback failed\nOK\n"); return; }
    printf("POST:"); for(int i=0;i<ne;i++) printf(" %02x",back[i]); printf("\n");
    printf("%s\nOK\n", memcmp(back,nw,ne) ? "MISMATCH" : "PATCHED-OK");
}

/* ---- instrumented-kstuff diagnostics: kekcall 8 = kread, 9 = kdata_base ---- */
#define KEKCALL_KREAD 0x0000000800000027ull
#define KEKCALL_KDATA 0x0000000900000027ull

static void cmd_kdbase(void) {
    int64_t r = kekcall(0, 0, 0, 0, 0, 0, KEKCALL_KDATA);
    printf("KDATA_BASE %#lx (ret=%ld)\nOK\n", (unsigned long)r, (long)r);
}

static uint8_t kbuf[4096];
static uint8_t kpost[4096];
static int kread_into(uint64_t va, void *dst, uint32_t sz) {
    int64_t r = kekcall((uint64_t)(uintptr_t)dst, va, sz, 0, 0, 0, KEKCALL_KREAD);
    return r == (int64_t)sz ? 0 : -1;
}
static int kread(uint64_t va, uint32_t sz) { return kread_into(va, kbuf, sz); }

static void cmd_kread(void) {
    uint64_t va; unsigned long sz;
    if (ntok < 3) { printf("ERR usage: kread <kva_hex> <size>\nOK\n"); return; }
    va = strtoull(tok[1], NULL, 0); sz = strtoul(tok[2], NULL, 0);
    if (!sz || sz > sizeof(kbuf)) { printf("ERR size 1..4096\nOK\n"); return; }
    if (kread(va, (uint32_t)sz)) { printf("ERR kread failed\nOK\n"); return; }
    for (unsigned long i = 0; i < sz; i += 16) {
        printf("%016lx:", (unsigned long)(va + i));
        for (unsigned long j = 0; j < 16 && i + j < sz; j++) printf(" %02x", kbuf[i + j]);
        printf("  ");
        for (unsigned long j = 0; j < 16 && i + j < sz; j++) {
            unsigned char c = kbuf[i + j];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }
    printf("OK\n");
}

/* kscan [start_off] [size_hex]: scan kernel memory for "can't support APR",
 * print hit VA + 32 bytes before and the following 96 bytes. */
static void cmd_kscan(void) {
    static const char needle[] = "can't support APR";
    int64_t kb = kekcall(0, 0, 0, 0, 0, 0, KEKCALL_KDATA);
    uint64_t start, size, va;
    int hits = 0, pages = 0;
    if (kb <= 0) { printf("ERR kdata base unavailable (%ld)\nOK\n", (long)kb); return; }
    start = (uint64_t)kb - 0x02000000ull;
    size = 0x04000000ull;
    if (ntok >= 2) start = (uint64_t)kb + (int64_t)strtoll(tok[1], NULL, 0);
    if (ntok >= 3) size = strtoull(tok[2], NULL, 0);
    printf("KSCAN base=%#lx start=%#lx size=%#lx\n", (unsigned long)kb,
           (unsigned long)start, (unsigned long)size);
    for (va = start; va < start + size; va += 0x1000) {
        if (kread(va, 0x1000)) continue;
        pages++;
        for (int i = 0; i + (int)sizeof(needle) <= 0x1000; i++) {
            if (kbuf[i] != (uint8_t)needle[0]) continue;
            if (memcmp(kbuf + i, needle, sizeof(needle) - 1)) continue;
            printf("HIT %#lx : %s\n", (unsigned long)(va + i), (char *)kbuf + i);
            if (i >= 32) {
                printf("  pre : ");
                for (int j = i - 32; j < i; j++) printf("%c", (kbuf[j] >= 32 && kbuf[j] < 127) ? kbuf[j] : '.');
                printf("\n");
            }
            if (kread_into(va + 0x1000, kpost, 0x1000) == 0) {
                printf("  post: ");
                for (int j = 0; j < 96; j++) printf("%c", (kpost[j] >= 32 && kpost[j] < 127) ? kpost[j] : '.');
                printf("\n  posthex:");
                for (int j = 0; j < 48; j++) printf(" %02x", kpost[j]);
                printf("\n");
            }
            hits++;
        }
    }
    printf("KSCAN pages=%d hits=%d\nOK\n", pages, hits);
}

/* pprcheck: is the kstuff kekcall path present? */
static void cmd_pprcheck(void) {
    int64_t r = kekcall(0, 0, 0, 0, 0, 0, 0xffffffff00000027ull);
    printf("kekcall check ret=%ld (0=kstuff present)\nOK\n", (long)r);
}

/* pprsnap: dump the kstuff shared-area snapshot (KSTUFF_OBS builds) and scan
 * for the fpkg hook markers: PPRMBX33 (verifyImage mailbox), PPRARM08 (latch),
 * PPRKIDX1, PPRPUT01, PPRCLR01 — tells whether the plaintext hook fired. */
static void cmd_pprsnap(void) {
    static uint8_t buf[65536];
    int64_t r;
    int i, n = 0;
    memset(buf, 0, sizeof(buf));
    r = kekcall((uint64_t)(uintptr_t)buf, sizeof(buf), 0, 0, 0, 0, 0x0000000600000027ull);
    printf("pprsnap ret=%ld (scan anyway)\n", (long)r);
    for (i = 0; i + 8 <= (int)sizeof(buf); i += 8) {
        uint64_t w;
        memcpy(&w, buf + i, 8);
        if (w == 0x5050524d42583333ull || /* PPRMBX33 */
            w == 0x50505241524d3038ull || /* PPRARM08 */
            w == 0x5050524b49445831ull || /* PPRKIDX1 */
            w == 0x5050525055543031ull || /* PPRPUT01 */
            w == 0x505052434c523031ull) { /* PPRCLR01 */
            uint64_t w2 = 0;
            memcpy(&w2, buf + i + 8, 8);
            printf("MARKER %016lx @%#x next=%016lx\n", (unsigned long)w, i, (unsigned long)w2);
            n++;
        }
    }
    printf("markers=%d\nOK\n", n);
}

/* globals declared below for the dispatcher; tentative re-declaration here so
 * the system-API commands can live in their own section */
static char *tok[256];
static int ntok;

/* ---- system pkg-mount API (libSceFsInternalForVsh.sprx) ----------------
 * The Sony VSH wrapper does the full pkg mount internally (LVD attach,
 * option set, PPRPLAIN staging/kstuff interplay). Struct layout from the
 * only public caller: hzhreal/PS4-vsh-utils source/pkg.c (MountGamePkgOpt).
 * We call Init first and only override the two known fields, so trailing
 * struct growth is tolerated. */
typedef struct {
    const char *pfsImgFile;   /* +0  */
    uint8_t     cache[64];    /* +8  */
    char       *budgetid;     /* +72 */
} mount_pkg_opt_t;

static void *load_fs_internal(void)
{
    static void *h;
    if (!h)
        h = dlopen("/system/priv/lib/libSceFsInternalForVsh.sprx", RTLD_NOW);
    return h;
}

static void cmd_mountpkg(void) {
    /* usage: mountpkg <pkg_path> <mountpoint> [game|ppr] [u1 u2 u3]
     * The wrapper takes three opaque ints after the key (the native two-stage
     * flow mounts app0-nest then app0 — these are the leading stage selectors). */
    void *h;
    int (*init_opt)(void *);
    int (*mount_pkg)(void *, const char *, const void *, int, int, int);
    uint8_t optbuf[512];
    uint8_t key[64];
    uint8_t keybuf[32] = {0};
    const char *sym_init, *sym_mount;
    int ret, u1 = 0, u2 = 0, u3 = 0;

    if (ntok < 3) {
        printf("ERR usage: mountpkg <pkg_path> <mountpoint> [game|ppr] [u1 u2 u3]\nOK\n");
        return;
    }
    int argi = 3;
    if (ntok > argi && !strcmp(tok[argi], "ppr")) {
        sym_init = "sceFsInitMountPprPkgOpt";
        sym_mount = "sceFsMountPprPkg";
        argi++;
    } else if (ntok > argi && !strcmp(tok[argi], "game")) {
        sym_init = "sceFsInitMountGamePkgOpt";
        sym_mount = "sceFsMountGamePkg";
        argi++;
    } else {
        sym_init = "sceFsInitMountPprPkgOpt";
        sym_mount = "sceFsMountPprPkg";
    }
    /* optional repeatable raw fills: mountpkg <pkg> <mnt> [game|ppr] [fill <off> <val>]...
     * (i_pConfig/sblock-offset hypothesis; applied AFTER Init) */
    uint64_t fills[8][2];
    int nfills = 0;
    while (ntok > argi + 2 && !strcmp(tok[argi], "fill") && nfills < 8) {
        long off = strtol(tok[argi + 1], NULL, 0);
        uint64_t val = (uint64_t)strtoull(tok[argi + 2], NULL, 0);
        if (off >= 0 && off <= 500 && (off & 7) == 0) {
            fills[nfills][0] = (uint64_t)off;
            fills[nfills][1] = val;
            nfills++;
        }
        argi += 3;
    }
    /* optional mountKey: key <hex>=... 64 hex chars = the 32-byte EKPFS
     * (LibProsperoPkg Crypto.ComputeKeys — the debug/pkg key derivation) */
    if (ntok > argi + 1 && !strcmp(tok[argi], "key")) {
        const char *hx = tok[argi + 1];
        for (int k2 = 0; k2 < 32; k2++) {
            unsigned int byte;
            if (sscanf(hx + k2 * 2, "%2x", &byte) != 1) break;
            keybuf[k2] = (uint8_t)byte;
        }
        printf("KEY %.16s...\n", hx);
        argi += 2;
    }
    h = load_fs_internal();
    if (!h) { printf("ERR dlopen libSceFsInternalForVsh\nOK\n"); return; }
    init_opt = (int (*)(void *))dlsym(h, sym_init);
    mount_pkg = (int (*)(void *, const char *, const void *, int, int, int))
                dlsym(h, sym_mount);
    if (!init_opt || !mount_pkg) {
        printf("ERR dlsym %s/%s init=%p mount=%p\nOK\n",
               sym_init, sym_mount, (void *)init_opt, (void *)mount_pkg);
        return;
    }
    /* generous ZEROED buffer: the PS5 opt struct is larger than the PS4
     * layout (MountGamePkgOpt 80 B); unmapped tail fields were stack garbage
     * ("VolumeSize ... is too large"). Init fills the known fields; we then
     * override only the two documented ones and dump the first qwords so the
     * transcript records the real layout. */
    memset(optbuf, 0, sizeof(optbuf));
    if (init_opt(optbuf) < 0) { printf("ERR %s failed\nOK\n", sym_init); return; }
    *(const char **)optbuf = tok[1];                 /* +0  pfsImgFile */
    *(char **)(optbuf + 72) = "system";              /* +72 budgetid   */
    /* +0x10 = writable name-buffer pointer (the mode code strncpy's the
     * budget name e.g. "game" into it — NULL here caused the payload crash) */
    static char namebuf[32];
    memset(namebuf, 0, sizeof(namebuf));
    *(char **)(optbuf + 16) = namebuf;
    /* +0x80 = the MOUNT PATH pointer (disassembly: _MountGamePkg's rsi comes
     * from opt+0x80 — "Mount path not specified" = NULL here) */
    *(const char **)(optbuf + 0x80) = tok[2];
    /* +0x18 = the KEY pointer (disassembly: rdx = opt+0x18 into _MountGamePkg) */
    *(const void **)(optbuf + 0x18) = key;
    for (int k = 0; k < nfills; k++) {
        memcpy(optbuf + fills[k][0], &fills[k][1], 8);
        if (nfills <= 4)
            printf("FILL +%lu = %#lx\n", (unsigned long)fills[k][0],
                   (unsigned long)fills[k][1]);
    }
    if (nfills > 4) printf("NFILLS %d\n", nfills);
    printf("INIT ");
    for (int q = 0; q < 32; q++)
        printf("%016lx ", (unsigned long)((uint64_t *)optbuf)[q]);
    printf("\n");
    memset(key, 0, sizeof(key));
    memcpy(key, keybuf, 32);
    /* mountPath buffer convention (RE + field evidence): {int32 handle_out;
     * char path[]} — the wrapper reads the path at +4 and returns the mount
     * handle at +0 (-1 = failure). Passing the path at +0 made every mount a
     * phantom (it resolved "/p1mnt"). */
    uint8_t pathbuf[256];
    memset(pathbuf, 0, sizeof(pathbuf));
    strncpy((char *)pathbuf + 4, tok[2], sizeof(pathbuf) - 5);
    printf("ARGS u1=%d u2=%d u3=%d\n", u1, u2, u3);
    ret = mount_pkg(optbuf, pathbuf, key, u1, u2, u3);
    {
        int32_t handle;
        memcpy(&handle, pathbuf, 4);
        printf("HANDLE %d\n", (int)handle);
    }
    /* the wrapper REWRITES the mountPath buffer (first bytes) — dump it so the
     * caller can find the resolved mount path */
    printf("PATHHEX ");
    for (int i = 0; i < 16; i++) printf("%02x", (unsigned char)tok[2][i]);
    printf(" PATHSTR %s\n", tok[2]);
    if (ret < 0) printf("ERR %s ret=0x%x\n", sym_mount, (unsigned)ret);
    else printf("OK %s %s -> %s\n", sym_mount, tok[1], tok[2]);
    printf("NAMEBUF %s\n", namebuf);
    printf("OK\n");
}

static void cmd_umountpkg(void) {
    /* usage: umountpkg <mountpoint> [game|ppr] */
    void *h;
    int (*init_u)(void *);
    int (*umount_pkg)(void *, const char *, int, int);
    uint8_t opt[256];
    const char *sym_init, *sym_umount;
    int ret;

    if (ntok < 2) {
        printf("ERR usage: umountpkg <mountpoint> [game|ppr]\nOK\n");
        return;
    }
    if (ntok >= 3 && !strcmp(tok[2], "ppr")) {
        sym_init = "sceFsInitUmountPprPkgOpt";
        sym_umount = "sceFsUmountPprPkg";
    } else {
        sym_init = "sceFsInitUmountGamePkgOpt";
        sym_umount = "sceFsUmountGamePkg";
    }
    h = load_fs_internal();
    if (!h) { printf("ERR dlopen libSceFsInternalForVsh\nOK\n"); return; }
    init_u = (int (*)(void *))dlsym(h, sym_init);
    umount_pkg = (int (*)(void *, const char *, int, int))dlsym(h, sym_umount);
    if (!init_u || !umount_pkg) {
        printf("ERR dlsym %s/%s\nOK\n", sym_init, sym_umount);
        return;
    }
    memset(opt, 0, sizeof(opt));
    if (init_u(opt) < 0) { printf("ERR %s failed\nOK\n", sym_init); return; }
    *(const char **)opt = tok[1]; /* umount opts carry the mount path (+0) */
    ret = umount_pkg(opt, tok[1], 0, 1);
    if (ret < 0) printf("ERR %s ret=0x%x\n", sym_umount, (unsigned)ret);
    else printf("OK %s %s\n", sym_umount, tok[1]);
    printf("OK\n");
}

/* Fixed script paths (no argv support guaranteed under hbldr). */
#define P1_SCRIPT_PATH "/mnt/usb0/diag/p1.cmd"
#define P1_OUT_PATH    "/mnt/usb0/diag/p1.out"

/* ---- ioctl macros (FreeBSD) — only nsfs_add still needs an _IOW; the LVD
 * ioctls are the literal constants from types.h (lvd_types.h). ----
 * The SDK's <sys/ioccom.h> already defines these; undef first to keep the
 * encodings byte-identical to the original tool.c (they are numerically the
 * same, but this documents intent and avoids -Wmacro-redefined noise). */
#undef _IOC
#undef _IOW
#undef _IOWR
#undef _IO
#define _IOC_NRBITS   8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS  2
#define _IOC_NRSHIFT  0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT+_IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT+_IOC_TYPEBITS)
#define _IOC_DIRSHIFT  (_IOC_SIZESHIFT+_IOC_SIZEBITS)
#define _IOC_WRITE 1U
#define _IOC_READ  2U
#define _IOC(dir,type,nr,size) \
    ((dir)<<_IOC_DIRSHIFT | (uint32_t)(type)<<_IOC_TYPESHIFT | \
     (nr)<<_IOC_NRSHIFT | (uint32_t)(size)<<_IOC_SIZESHIFT)
#define _IOW(type,nr,size)  _IOC(_IOC_WRITE,(type),(nr),sizeof(size))
#define _IOWR(type,nr,size) _IOC(_IOC_READ|_IOC_WRITE,(type),(nr),sizeof(size))
#define _IO(type,nr)        _IOC(0,(type),(nr),0)

extern int nmount(struct iovec *iov, unsigned int n, int flags);

/* default attach option_len — pfsc_mount.c PFS profile passes flags=0x1C
 * (verdict §1 / p1-review-verdict.md:11). Overridable via the 4th arg. */
#define LVD_DEFAULT_OPTION_LEN 0x1Cu

/* ---- helpers ---- */
static char line[4096];
/* Token capacity: full P1c layout option set = 21 tokens (mntfs + fstype +
 * source + mountpoint + 17 options); 256 covers the all-slot opt fills. */
static char *tok[256];
static int ntok;

/* `last` device token: device_id of the most recent SUCCESSFUL lvdattach in
 * this process (set only when the attach returns device_id >= 0). Flat
 * scripts use it because they cannot echo the printed device_id back. */
static int last_dev = -1;

/* Script mode + one-mount rule (R2/R5): once a mount is established, further
 * lvdattach/mntfs in the SAME script are refused (a flat script cannot
 * branch on success). Stdin mode (attended) is not restricted. */
static int script_mode = 0;
static int mount_established = 0;

static void cmd_nsfs_add(void) {
    struct nsfs_redirect { char source[256]; char destination[256];
                           uint32_t type; uint32_t reserved[4]; } r;
    if (ntok < 4) { printf("ERR usage: nsfs_add <src> <dst> <type>\nOK\n"); return; }
    {
        int fd = open("/dev/nsfsctl", O_RDWR);
        if (fd < 0) { printf("ERR open nsfsctl: %s\nOK\n", strerror(errno)); return; }
        memset(&r, 0, sizeof(r));
        strncpy(r.source, tok[1], sizeof(r.source)-1);
        strncpy(r.destination, tok[2], sizeof(r.destination)-1);
        r.type = (uint32_t)atoi(tok[3]);
        {
            int ret = ioctl(fd, _IOW('n', 1, struct nsfs_redirect), &r);
            if (ret < 0) printf("ERR nsfs_add: %s\n", strerror(errno));
            else printf("OK nsfs_add %s -> %s type=%s\n", tok[1], tok[2], tok[3]);
        }
        close(fd);
    }
    printf("OK\n");
}

static void cmd_mkdir(void) {
    if (ntok < 2) { printf("ERR usage: mkdir <path>\nOK\n"); return; }
    if (mkdir(tok[1], 0755) < 0) {
        if (errno == EEXIST) printf("OK mkdir %s (exists)\n", tok[1]);
        else printf("ERR mkdir %s: %s\n", tok[1], strerror(errno));
    } else {
        printf("OK mkdir %s\n", tok[1]);
    }
    printf("OK\n");
}

static void cmd_rmdir(void) {
    /* usage: rmdir <path>
     * Exact name  -> plain rmdir(2) (empty dirs only).
     * Leaf with '*'/'?' -> readdir the parent and rmdir(2) every entry
     * matching the leaf pattern (p1_glob_match). Scratch-cleanup primitive
     * for stray dirs like the "/mnt/p1mnt\n" left by the tokenizer defect
     * (shsrv strips newlines and ftpsrv cannot type/embed an LF name). */
    char dirbuf[4096], full[8192];
    const char *leaf, *slash;
    int wild;

    if (ntok < 2) { printf("ERR usage: rmdir <path>\nOK\n"); return; }

    slash = strrchr(tok[1], '/');
    if (slash != NULL) {
        size_t dlen = (size_t)(slash - tok[1]);
        if (dlen == 0) { dirbuf[0] = '/'; dirbuf[1] = 0; }
        else { memcpy(dirbuf, tok[1], dlen); dirbuf[dlen] = 0; }
        leaf = slash + 1;
    } else {
        memcpy(dirbuf, ".", 2);
        leaf = tok[1];
    }

    wild = (strchr(leaf, '*') != NULL || strchr(leaf, '?') != NULL);
    if (!wild) {
        if (rmdir(tok[1]) < 0) printf("ERR rmdir %s: %s\n", tok[1], strerror(errno));
        else printf("OK rmdir %s\n", tok[1]);
        printf("OK\n");
        return;
    }

    {
        DIR *d = opendir(dirbuf);
        struct dirent *e;
        if (!d) { printf("ERR rmdir: opendir %s: %s\nOK\n", dirbuf, strerror(errno)); return; }
        while ((e = readdir(d))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            if (!p1_glob_match(leaf, e->d_name)) continue;
            if (dirbuf[0] == '/' && dirbuf[1] == 0)
                snprintf(full, sizeof(full), "/%s", e->d_name);
            else
                snprintf(full, sizeof(full), "%s/%s", dirbuf, e->d_name);
            if (rmdir(full) < 0) printf("ERR rmdir %s: %s\n", full, strerror(errno));
            else printf("OK rmdir %s\n", full);
        }
        closedir(d);
    }
    printf("OK\n");
}

static void cmd_lvdattach(void) {
    /* usage: lvdattach <path> <sector> <img_type> [option_len] [secondary]
     * secondary = LVD mapping unit (sm_mount_defs.h: optimized profile uses
     * 64 KiB = 0x10000; defaults to <sector> when omitted). */
    struct lvd_kernel_layer_t layer;
    struct lvd_ioctl_attach_t req;
    struct stat st;
    unsigned int sector = 0, imgtype = 0;
    unsigned int optlen = LVD_DEFAULT_OPTION_LEN;
    unsigned int secondary = 0;
    int fd, ret;

    if (ntok < 4) {
        printf("ERR usage: lvdattach <path> <sector> <img_type> [option_len] [secondary]\nOK\n");
        return;
    }
    if (script_mode && mount_established) {
        printf("ERR one-mount rule: mount already established - refusing\nOK\n");
        return;
    }
    /* numeric args via the host-tested helper (p1_parse_lvd_nums):
     * sector=u32, img_type=u16 (hex 0x.. accepted), option_len=u16 */
    {
        int bad = p1_parse_lvd_nums(tok[2], tok[3], ntok >= 5 ? tok[4] : NULL,
                                    &sector, &imgtype, &optlen);
        if (bad == 1) { printf("ERR sector not a number: %s\nOK\n", tok[2]); return; }
        if (bad == 2) { printf("ERR img_type not a u16: %s\nOK\n", tok[3]); return; }
        if (bad == 3) { printf("ERR option_len not a u16: %s\nOK\n", tok[4]); return; }
    }
    /* secondary_unit: LVD mapping granularity (sm_mount_defs.h optimized
     * profile = 0x10000). Defaults to <sector> when omitted. */
    secondary = sector;
    if (ntok >= 6 && p1_parse_u32(tok[5], &secondary) != 0) {
        printf("ERR secondary not a number: %s\nOK\n", tok[5]); return;
    }
    if (stat(tok[1], &st) < 0) {
        printf("ERR stat %s: %s\nOK\n", tok[1], strerror(errno)); return;
    }

    /* layer profile: pfsc_mount.c lvd_layer_t (verdict §1) */
    memset(&layer, 0, sizeof(layer));
    layer.source_type = 1;
    layer.entry_flags = 0x1;
    layer.path = tok[1];
    layer.offset = 0;
    layer.size = (uint64_t)st.st_size;

    /* attach profile: pfsc_mount.c lvd_attach_t (verdict §1) */
    memset(&req, 0, sizeof(req));
    req.io_version = 0;
    req.device_id = -1;
    req.sector_size_0 = sector;
    req.sector_size_1 = secondary; /* mapping unit; optimized profile = 0x10000 */
    req.option_len = (uint16_t)optlen;
    req.image_type = (uint16_t)imgtype;
    req.layer_count = 1;
    req.device_size = layer.size;
    req.layers_ptr = &layer;

    fd = open("/dev/lvdctl", O_RDWR);
    if (fd < 0) { printf("ERR open lvdctl: %s\nOK\n", strerror(errno)); return; }

    ret = ioctl(fd, SCE_LVD_IOC_ATTACH, &req);
    if (ret < 0) {
        printf("ERR lvdattach: %s\n", strerror(errno));
    } else {
        /* req.device_id is IN/OUT: the kernel returns the allocated unit.
         * That number is the /dev/lvdN used as nmount "from" (or `last`). */
        if (req.device_id >= 0)
            last_dev = req.device_id;
        printf("OK lvdattach %s sector=%u img_type=%u option_len=0x%x"
               " device_id=%d /dev/lvd%d\n",
               tok[1], sector, imgtype, optlen, req.device_id, req.device_id);
    }
    close(fd);
    printf("OK\n");
}

static void cmd_lvddetach(void) {
    /* usage: lvddetach <device_id|last>  (device_id returned by lvdattach) */
    struct lvd_ioctl_detach_t d;
    int devid = 0, fd, ret, r;

    if (ntok < 2) { printf("ERR usage: lvddetach <device_id|last>\nOK\n"); return; }
    r = p1_resolve_last(tok[1], last_dev, &devid);
    if (r < 0) { printf("ERR no last device\nOK\n"); return; }
    if (r == 0 && p1_parse_i32(tok[1], &devid) != 0) {
        printf("ERR device_id not a number: %s\nOK\n", tok[1]); return;
    }

    memset(&d, 0, sizeof(d));
    d.device_id = (int32_t)devid;

    fd = open("/dev/lvdctl", O_RDWR);
    if (fd < 0) { printf("ERR open lvdctl: %s\nOK\n", strerror(errno)); return; }

    ret = ioctl(fd, SCE_LVD_IOC_DETACH, &d);
    if (ret < 0) printf("ERR lvddetach %d: %s\n", devid, strerror(errno));
    else {
        /* R8: successful detach clears the remembered `last` device (of
         * `last` or an explicit matching id) — a stale `last` could detach
         * a reassigned system device in a later block. */
        last_dev = p1_last_after_detach(last_dev, devid);
        printf("OK lvddetach %d (/dev/lvd%d)\n", devid, devid);
    }
    close(fd);
    printf("OK\n");
}

static void cmd_umount(void) {
    int flags = 0;
    if (ntok < 2) { printf("ERR usage: umount <path> [force]\nOK\n"); return; }
    if (ntok >= 3 && !strcmp(tok[2], "force")) flags = MNT_FORCE;
    if (unmount(tok[1], flags) < 0) printf("ERR umount %s: %s\n", tok[1], strerror(errno));
    else printf("OK umount %s flags=0x%x\n", tok[1], flags);
    printf("OK\n");
}

static void cmd_mntfs(void) {
    /* 6 base iovecs + up to 2 per option; room for the documented fallback
     * option set (pfsc_mount.c) with margin. Overflow is an ERROR, never a
     * silent drop: a partial option set would pollute the probe. */
    struct iovec iov[6 + 32];
    char frombuf[32];
    char *errbuf = NULL; /* errmsg is a kernel WRITEBACK buffer — read it after nmount */
    const char *from;
    int i = 0, a, r, mntflags = 0;

    if (ntok < 4) { printf("ERR usage: mntfs <fstype> <source|last> <mountpoint> [opt...]\nOK\n"); return; }
    if (script_mode && mount_established) {
        printf("ERR one-mount rule: mount already established - refusing\nOK\n");
        return;
    }

    /* device token: literal `last` -> /dev/lvd<last_dev> at command time */
    {
        int devid = -1;
        r = p1_resolve_last(tok[2], last_dev, &devid);
        if (r < 0) { printf("ERR no last device\nOK\n"); return; }
        if (r == 1) {
            if (p1_lvd_path(devid, frombuf, sizeof(frombuf)) != 0) {
                printf("ERR no last device\nOK\n"); return;
            }
            from = frombuf;
        } else {
            from = tok[2];
        }
    }

    iov[i].iov_base = "fstype"; iov[i].iov_len = 7; i++;
    iov[i].iov_base = tok[1];   iov[i].iov_len = strlen(tok[1])+1; i++;
    iov[i].iov_base = "from";   iov[i].iov_len = 5; i++;
    iov[i].iov_base = (void *)from; iov[i].iov_len = strlen(from)+1; i++;
    iov[i].iov_base = "fspath"; iov[i].iov_len = 7; i++;
    iov[i].iov_base = tok[3];   iov[i].iov_len = strlen(tok[3])+1; i++;

    for (a = 4; a < ntok && i + 2 <= (int)(sizeof(iov)/sizeof(iov[0])); a++) {
        char *key = tok[a], *val = NULL;
        /* '@flag' tokens are nmount(2) FLAG directives, not mount options:
         * the native ppr pkg mount uses MNT_RDONLY as the syscall flag; the
         * 'rdonly' option trips check_mount_option_ppr_pkg's consistency
         * check (proven live: errmsg writeback + klog line archaeology). */
        if (tok[a][0] == '@') {
            if (!strcmp(tok[a], "@rdonly")) mntflags |= MNT_RDONLY;
            else if (!strcmp(tok[a], "@force")) mntflags |= MNT_FORCE;
            else { printf("ERR mntfs: unknown flag directive %s\nOK\n", tok[a]); return; }
            continue;
        }
        p1_split_kv(tok[a], &key, &val);
        if (val != NULL) {
            iov[i].iov_base = key; iov[i].iov_len = strlen(key)+1; i++;
            iov[i].iov_base = val; iov[i].iov_len = strlen(val)+1; i++;
            if (!strcmp(key, "errmsg")) errbuf = val;
        } else {
            /* bare flag: proper (name, NULL, 0) iovec PAIR — the kernel
             * decodes async/noatime/automounted as flags; a single iovec
             * would misalign every subsequent (name,value) pair. */
            iov[i].iov_base = key; iov[i].iov_len = strlen(key)+1; i++;
            iov[i].iov_base = NULL; iov[i].iov_len = 0; i++;
        }
    }
    if (a < ntok) { printf("ERR mntfs: option list too long\nOK\n"); return; }

    {
        int ret = nmount(iov, i, mntflags);
        if (ret < 0) printf("ERR mntfs: %s\n", strerror(errno));
        else {
            printf("OK mntfs %s %s -> %s (iov=%d)\n", tok[1], from, tok[3], i);
            mount_established = 1;
        }
        /* The kernel writes its reason into the errmsg buffer (SMP reads it the
         * same way). This is the primary diagnostic for option-set probing. */
        if (errbuf != NULL) printf("ERRMSG> %s\n", errbuf);
    }
    printf("OK\n");
}

static void cmd_ls(void) {
    DIR *d;
    struct dirent *e;
    if (ntok < 2) { printf("ERR usage: ls <path>\nOK\n"); return; }
    d = opendir(tok[1]);
    if (!d) { printf("ERR ls: %s\nOK\n", strerror(errno)); return; }
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        printf("%s\n", e->d_name);
    }
    closedir(d);
    printf("OK\n");
}

static void cmd_statfs(void) {
    struct statfs st;
    if (ntok < 2) { printf("ERR usage: statfs <path>\nOK\n"); return; }
    if (statfs(tok[1], &st) < 0) { printf("ERR statfs: %s\nOK\n", strerror(errno)); return; }
    printf("fstype=%lx bsize=%lu blocks=%lu bfree=%lu\n",
           (unsigned long)st.f_type, (unsigned long)st.f_bsize,
           (unsigned long)st.f_blocks, (unsigned long)st.f_bfree);
    printf("OK\n");
}

/* ---- shared tokenizer + dispatcher (stdin mode and script mode) ---- */
static void run_command(char *buf) {
    /* p1_tokenize_line = chomp + tokenize: without the chomp, fgets' trailing
     * '\n' is glued to the LAST token (live-run defect: "p1mnt\n", "0x1C\n"). */
    ntok = p1_tokenize_line(buf, tok, (int)(sizeof(tok)/sizeof(tok[0])));
    if (ntok < 0) { printf("ERR too many tokens\nOK\n"); fflush(stdout); return; }
    if (ntok == 0) return;
    if      (!strcmp(tok[0], "nsfs_add"))  cmd_nsfs_add();
    else if (!strcmp(tok[0], "mkdir"))     cmd_mkdir();
    else if (!strcmp(tok[0], "rmdir"))     cmd_rmdir();
    else if (!strcmp(tok[0], "lvdattach")) cmd_lvdattach();
    else if (!strcmp(tok[0], "lvddetach")) cmd_lvddetach();
    else if (!strcmp(tok[0], "umount"))    cmd_umount();
    else if (!strcmp(tok[0], "mntfs"))     cmd_mntfs();
    else if (!strcmp(tok[0], "mountpkg"))  cmd_mountpkg();
    else if (!strcmp(tok[0], "mountpkg2")) cmd_mountpkg2();
    else if (!strcmp(tok[0], "umountpkg")) cmd_umountpkg();
    else if (!strcmp(tok[0], "pprarm"))    cmd_pprarm();
    else if (!strcmp(tok[0], "pprfinal"))  cmd_pprfinal();
    else if (!strcmp(tok[0], "pprcheck"))  cmd_pprcheck();
    else if (!strcmp(tok[0], "kdbase"))    cmd_kdbase();
    else if (!strcmp(tok[0], "kkread"))    cmd_kkread();
    else if (!strcmp(tok[0], "kkxref"))    cmd_kkxref();
    else if (!strcmp(tok[0], "kpeek"))     cmd_kpeek();
    else if (!strcmp(tok[0], "kxref"))     cmd_kxref();
    else if (!strcmp(tok[0], "kphys"))     cmd_kphys();
    else if (!strcmp(tok[0], "dmem"))      cmd_dmem();
    else if (!strcmp(tok[0], "dmmap"))     cmd_dmmap();
    else if (!strcmp(tok[0], "kxrefp"))    cmd_kxref_p();
    else if (!strcmp(tok[0], "kmemtest"))  cmd_kmemtest();
    else if (!strcmp(tok[0], "kwalk"))     cmd_kwalk();
    else if (!strcmp(tok[0], "kmdbg"))     cmd_kmdbg();
    else if (!strcmp(tok[0], "kgetlong"))  cmd_kgetlong();
    else if (!strcmp(tok[0], "kpatch"))    cmd_kpatch();
    else if (!strcmp(tok[0], "kread"))     cmd_kread();
    else if (!strcmp(tok[0], "kscan"))     cmd_kscan();
    else if (!strcmp(tok[0], "kscan2"))    cmd_kscan_k();
    else if (!strcmp(tok[0], "pprsnap"))   cmd_pprsnap();
    else if (!strcmp(tok[0], "ls"))        cmd_ls();
    else if (!strcmp(tok[0], "statfs"))    cmd_statfs();
    else { printf("ERR unknown: %s\nOK\n", tok[0]); }
    fflush(stdout);
}

int main(void) {
    FILE *sf = fopen(P1_SCRIPT_PATH, "r");

    if (sf != NULL) {
        /* Script mode (unattended): every line goes through the same
         * tokenizer + dispatcher; a full transcript is written to
         * P1_OUT_PATH (echo of each command line, then its output).
         * If the transcript cannot be created, abort WITHOUT executing
         * anything (exit 1): an unattended probe without transcript is
         * not analyzable and could leave a mount unrecorded. */
        if (freopen(P1_OUT_PATH, "w", stdout) == NULL)
            return 1;
        script_mode = 1;
        printf("TOOL READY (script mode: %s -> %s)\n", P1_SCRIPT_PATH, P1_OUT_PATH);
        fflush(stdout);
        while (p1_script_next(sf, line, sizeof(line)) == 1) {
            printf("> %s\n", line); /* echo BEFORE dispatch (tokenize mutates) */
            fflush(stdout);
            run_command(line);
        }
        printf("SCRIPT EOF\n");
        fflush(stdout);
        fclose(sf);
        return 0; /* exit(0) at script EOF */
    }

    /* stdin mode (fallback when the script file is absent; unchanged) */
    printf("TOOL READY\n");
    fflush(stdout);
    while (fgets(line, sizeof(line), stdin))
        run_command(line);
    return 0;
}
