/* Minimal kstuff loader: embeds ps5-kstuff/payload.bin, maps its segments and
 * calls the kernel payload entry. No title manager / app.db / shellui parts.
 * Built only to deploy our instrumented kstuff (kekcall 8/9 diagnostics). */
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <machine/param.h>

#include <ps5/payload.h>
#include <ps5/klog.h>
#include <ps5/kernel.h>

asm(".section .rodata\n"
    ".global ___ps5_kstuff_payload_bin\n"
    "___ps5_kstuff_payload_bin:\n"
    ".incbin \"../ps5-kstuff/payload.bin\"\n");

extern char ___ps5_kstuff_payload_bin[];

int sceKernelSetProcessName(const char *name);

#define ROUND_PG(x) (((x) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))
#define TRUNC_PG(x) ((x) & ~(PAGE_SIZE - 1))
#define PFLAGS(x)   ((((x) & PF_R) ? PROT_READ  : 0) | \
                     (((x) & PF_W) ? PROT_WRITE : 0) | \
                     (((x) & PF_X) ? PROT_EXEC  : 0))

#define KEKCALL_KREAD 0x0000000800000027ull
#define KEKCALL_KDATA 0x0000000900000027ull

static int64_t kekcall(uint64_t a, uint64_t b, uint64_t c,
                       uint64_t d, uint64_t e, uint64_t f, uint64_t g)
{
    register uint64_t r10 __asm__("r10") = d;
    register uint64_t r8  __asm__("r8")  = e;
    register uint64_t r9  __asm__("r9")  = f;
    uint64_t ret; unsigned char is_error;
    __asm__ __volatile__("syscall"
        : "=a"(ret), "=@ccc"(is_error), "+r"(r10), "+r"(r8), "+r"(r9)
        : "a"(g), "D"(a), "S"(b), "d"(c)
        : "rcx", "r11", "memory");
    return is_error ? -(int64_t)ret : (int64_t)ret;
}

static int kfile(const char *s)
{
    int fd = open("/mnt/usb0/kdiag.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) return -1;
    write(fd, s, strlen(s));
    close(fd);
    return 0;
}

static void klogf(const char *fmt, ...)
{
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    kfile(b);
}

static void
pt_load(const void* image, void* base, Elf64_Phdr *phdr) {
    if(phdr->p_memsz && phdr->p_filesz) {
        memcpy(base + phdr->p_vaddr, image + phdr->p_offset, phdr->p_filesz);
    }
}

int main(void) {
    klog_printf("kdiag: loader start\n");
    sceKernelSetProcessName("kstuff.elf");
    Elf64_Ehdr *ehdr = (Elf64_Ehdr*)___ps5_kstuff_payload_bin;
    Elf64_Phdr *phdr = (Elf64_Phdr*)(___ps5_kstuff_payload_bin + ehdr->e_phoff);
    void *base = (void*)0x0000000926100000;
    uintptr_t min_vaddr = -1;
    uintptr_t max_vaddr = 0;
    size_t base_size;

    for(int i=0; i<ehdr->e_phnum; i++) {
        if(phdr[i].p_vaddr < min_vaddr) min_vaddr = phdr[i].p_vaddr;
        if(max_vaddr < phdr[i].p_vaddr + phdr[i].p_memsz)
            max_vaddr = phdr[i].p_vaddr + phdr[i].p_memsz;
    }
    min_vaddr = TRUNC_PG(min_vaddr);
    max_vaddr = ROUND_PG(max_vaddr);
    base_size = max_vaddr - min_vaddr;

    if((base=mmap(base, base_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }
    for(int i=0; i<ehdr->e_phnum; i++) {
        if(phdr[i].p_type == PT_LOAD) pt_load(___ps5_kstuff_payload_bin, base, &phdr[i]);
    }
    for(int i=0; i<ehdr->e_phnum; i++) {
        if(phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) continue;
        if(mprotect(base + phdr[i].p_vaddr, ROUND_PG(phdr[i].p_memsz),
                    PFLAGS(phdr[i].p_flags))) {
            perror("mprotect");
            return EXIT_FAILURE;
        }
    }

    void (*entry)(payload_args_t*) = base + ehdr->e_entry;
    payload_args_t* args = payload_get_args();

    uint64_t proc = kernel_get_proc(-1);
    uint64_t p_dynlib = kernel_getlong(proc + 0x3e8);
    uint64_t dynlib_eboot = kernel_getlong(p_dynlib + 0x00);
    uint64_t eboot_segments = kernel_getlong(dynlib_eboot + 0x40);
    kernel_setlong(eboot_segments + 0x08, 0);
    kernel_setlong(eboot_segments + 0x10, 0xFFFFFFFFFFFFFFFFL);

    klog_printf("kdiag: args=%lx kdata=%lx kpipe=%lx rwpipe=%lx rwpair=%lx out=%lx\n",
                (unsigned long)(uintptr_t)args,
                args ? (unsigned long)args->kdata_base_addr : 0ul,
                args ? (unsigned long)args->kpipe_addr : 0ul,
                args ? (unsigned long)(uintptr_t)args->rwpipe : 0ul,
                args ? (unsigned long)(uintptr_t)args->rwpair : 0ul,
                args ? (unsigned long)(uintptr_t)args->payloadout : 0ul);
    klog_printf("kdiag: calling payload entry\n");
    entry(args);
    klog_printf("kstuff instrumented payload up\n");
    sleep(2);
    {
        static unsigned char page[65536];
        uint64_t kb = (uint64_t)kekcall(0,0,0,0,0,0,KEKCALL_KDATA);
        uint64_t text, needle_va, target;
        int i, refs = 0, chunks = 0, fails = 0;
        klogf("== kdiag start ==\n");
        klogf("kdata=%lx\n", (unsigned long)kb);
        if (!kb) { klogf("no kdata\n"); for(;;) sleep(60); }
        text = kb - 0xC00000ull;
        needle_va = kb + 0x33EA07ull;
        target = needle_va - 26;
        klogf("text=%lx needle=%lx target=%lx\n",
              (unsigned long)text, (unsigned long)needle_va, (unsigned long)target);
        if (kekcall((uint64_t)(uintptr_t)page, needle_va, 32, 0,0,0, KEKCALL_KREAD) == 32) {
            char t[40]; int n = 0;
            while (n < 32 && page[n] >= 32 && page[n] < 127) { t[n] = (char)page[n]; n++; }
            t[n] = 0;
            klogf("needle ok: '%s'\n", t);
        } else {
            klogf("needle read FAILED\n");
        }
        if (kekcall((uint64_t)(uintptr_t)page, text, 32, 0,0,0, KEKCALL_KREAD) == 32) {
            klogf("text head: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                  page[0],page[1],page[2],page[3],page[4],page[5],page[6],page[7]);
        } else {
            klogf("text head read FAILED\n");
        }
        for (uint64_t va = text; va < kb; va += 0x10000) {
            unsigned long chunk = (kb - va) < 0x10000 ? (unsigned long)(kb - va) : 0x10000;
            if (kekcall((uint64_t)(uintptr_t)page, va, chunk, 0,0,0, KEKCALL_KREAD) != (int64_t)chunk) { fails++; continue; }
            chunks++;
            if ((chunks & 0x0f) == 0) klogf("prog %lx chunks=%d\n", (unsigned long)va, chunks);
            for (i = 0; i + 7 < (int)chunk; i++) {
                unsigned char b0 = page[i], b1 = page[i+1], b2 = page[i+2];
                if ((b0==0x48||b0==0x49||b0==0x4c||b0==0x4d) && b1==0x8d && (b2 & 0xC7)==0x05) {
                    int32_t d; __builtin_memcpy(&d, page+i+3, 4);
                    uint64_t t = va + i + 7 + (int64_t)d;
                    if (t == target) { klogf("XREF lea %lx\n", (unsigned long)(va+i)); refs++; }
                } else if ((b0==0x48||b0==0x49) && b1>=0xb8 && b1<=0xbf) {
                    uint64_t imm; __builtin_memcpy(&imm, page+i+2, 8);
                    if (imm == target) { klogf("XREF movabs %lx\n", (unsigned long)(va+i)); refs++; }
                }
            }
        }
        klogf("done chunks=%d fails=%d refs=%d\n== end ==\n", chunks, fails, refs);
    }
    for(;;) sleep(60);
}
