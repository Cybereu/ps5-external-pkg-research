/*
 * test_parse.c — host tests for parse.c (no PS5 toolchain required).
 * Build:  cc -Wall -o test_parse test_parse.c parse.c
 * Run:    ./test_parse
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#include "parse.h"
#include "lvd_types.h"

static int failures = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (!(cond)) { printf("FAIL: %s\n", msg); failures++; }            \
        else { printf("ok  : %s\n", msg); }                                \
    } while (0)

static void test_tokenize(void)
{
    char *tok[16];
    int n;

    {
        char a[] = "lvdattach /a/b.pkg 4096 2";
        n = p1_tokenize(a, tok, 16);
        CHECK(n == 4, "tokenize: plain 4 tokens");
        CHECK(n == 4 && !strcmp(tok[0], "lvdattach"), "tokenize: tok0=lvdattach");
        CHECK(n == 4 && !strcmp(tok[1], "/a/b.pkg"), "tokenize: tok1=path");
        CHECK(n == 4 && !strcmp(tok[2], "4096"), "tokenize: tok2=4096");
        CHECK(n == 4 && !strcmp(tok[3], "2"), "tokenize: tok3=2");
    }
    {
        char b[] = "mntfs ppr_pfs \"/dev/lvd0\" \"/mnt/p1 probe\" ro "
                   "sblock_offset=0x51520000";
        n = p1_tokenize(b, tok, 16);
        CHECK(n == 6, "tokenize: quoted 6 tokens");
        CHECK(n == 6 && !strcmp(tok[3], "/mnt/p1 probe"),
              "tokenize: quoted path keeps space");
        CHECK(n == 6 && !strcmp(tok[5], "sblock_offset=0x51520000"),
              "tokenize: kv token preserved");
    }
    {
        char c[] = "   \t  ";
        n = p1_tokenize(c, tok, 16);
        CHECK(n == 0, "tokenize: whitespace-only -> 0");
    }
    {
        char d[] = "a b c d e f g h i j k l m n o p q";
        n = p1_tokenize(d, tok, 16);
        CHECK(n == -1, "tokenize: overflow -> -1");
    }
}

static void test_split_kv(void)
{
    char *k = NULL, *v = NULL;
    int r;

    {
        char a[] = "sblock_offset=0x51520000";
        r = p1_split_kv(a, &k, &v);
        CHECK(r == 1, "split_kv: key=value -> 1");
        CHECK(r == 1 && !strcmp(k, "sblock_offset"), "split_kv: key");
        CHECK(r == 1 && v != NULL && !strcmp(v, "0x51520000"), "split_kv: value");
    }
    {
        char b[] = "ro";
        r = p1_split_kv(b, &k, &v);
        CHECK(r == 0, "split_kv: bare flag -> 0");
        CHECK(r == 0 && !strcmp(k, "ro"), "split_kv: bare key");
        CHECK(r == 0 && v == NULL, "split_kv: bare value NULL");
    }
    {
        char c[] = "a=b=c";
        r = p1_split_kv(c, &k, &v);
        CHECK(r == 1, "split_kv: multi '=' -> 1");
        CHECK(r == 1 && !strcmp(k, "a"), "split_kv: multi key");
        CHECK(r == 1 && v != NULL && !strcmp(v, "b=c"), "split_kv: multi value");
    }
    {
        char d[] = "=v";
        CHECK(p1_split_kv(d, &k, &v) == -1, "split_kv: empty key -> -1");
    }
}

static void test_parse_u32(void)
{
    unsigned int o = 0;

    CHECK(p1_parse_u32("4096", &o) == 0 && o == 4096u, "u32: 4096");
    CHECK(p1_parse_u32("0x51520000", &o) == 0 && o == 0x51520000u, "u32: hex 0x51520000");
    CHECK(p1_parse_u32("0x02", &o) == 0 && o == 2u, "u32: 0x02 -> 2");
    CHECK(p1_parse_u32("2", &o) == 0 && o == 2u, "u32: 2");
    CHECK(p1_parse_u32("0x82", &o) == 0 && o == 0x82u, "u32: 0x82");
    CHECK(p1_parse_u32("abc", &o) == -1, "u32: abc -> -1");
    CHECK(p1_parse_u32("", &o) == -1, "u32: empty -> -1");
    CHECK(p1_parse_u32("12x", &o) == -1, "u32: trailing junk -> -1");
    CHECK(p1_parse_u32("0xffffffff", &o) == 0 && o == 0xffffffffu, "u32: max");
    CHECK(p1_parse_u32("0x100000000", &o) == -1, "u32: overflow -> -1");
}

static void test_parse_i32(void)
{
    int o = 0;

    CHECK(p1_parse_i32("3", &o) == 0 && o == 3, "i32: 3");
    CHECK(p1_parse_i32("-1", &o) == 0 && o == -1, "i32: -1 (device_id=-1)");
    CHECK(p1_parse_i32("0", &o) == 0 && o == 0, "i32: 0");
    CHECK(p1_parse_i32("2147483647", &o) == 0 && o == 2147483647, "i32: INT32_MAX");
    CHECK(p1_parse_i32("-2147483648", &o) == 0 && o == (-2147483647 - 1), "i32: INT32_MIN");
    CHECK(p1_parse_i32("2147483648", &o) == -1, "i32: overflow -> -1");
    CHECK(p1_parse_i32("-2147483649", &o) == -1, "i32: underflow -> -1");
    CHECK(p1_parse_i32("0x10", &o) == 0 && o == 16, "i32: hex 0x10");
    CHECK(p1_parse_i32("-0x1", &o) == 0 && o == -1, "i32: hex -0x1");
    CHECK(p1_parse_i32("", &o) == -1, "i32: empty -> -1");
    CHECK(p1_parse_i32("-", &o) == -1, "i32: bare minus -> -1");
    CHECK(p1_parse_i32("12x", &o) == -1, "i32: trailing junk -> -1");
    CHECK(p1_parse_i32("abc", &o) == -1, "i32: abc -> -1");
    CHECK(p1_parse_i32(NULL, &o) == -1, "i32: NULL -> -1");
}

/*
 * u16 parser (LVD img_type / option_len). Bug fix: must accept the 0x
 * prefix exactly like the u32/i32 parsers (live run failed on
 * "lvdattach ... 2 0x1C" -> "ERR option_len not a u16: 0x1C").
 */
static void test_parse_u16(void)
{
    unsigned int o = 0;

    CHECK(p1_parse_u16("0x1C", &o) == 0 && o == 28u, "u16: 0x1C -> 28");
    CHECK(p1_parse_u16("0x82", &o) == 0 && o == 130u, "u16: 0x82 -> 130");
    CHECK(p1_parse_u16("0X10", &o) == 0 && o == 16u, "u16: 0X10 -> 16 (uppercase X)");
    CHECK(p1_parse_u16("28", &o) == 0 && o == 28u, "u16: decimal 28 -> 28");
    CHECK(p1_parse_u16("65535", &o) == 0 && o == 65535u, "u16: 65535 (max)");
    CHECK(p1_parse_u16("65536", &o) == -1, "u16: 65536 overflow -> -1");
    CHECK(p1_parse_u16("0xFFFF", &o) == 0 && o == 65535u, "u16: 0xFFFF (max hex)");
    CHECK(p1_parse_u16("0x10000", &o) == -1, "u16: 0x10000 overflow -> -1");
    CHECK(p1_parse_u16("0x", &o) == -1, "u16: bare 0x -> -1");
    CHECK(p1_parse_u16("", &o) == -1, "u16: empty -> -1");
    CHECK(p1_parse_u16(NULL, &o) == -1, "u16: NULL -> -1");
    CHECK(p1_parse_u16("12x", &o) == -1, "u16: trailing junk -> -1");
    CHECK(p1_parse_u16("-1", &o) == -1, "u16: negative -> -1");
}

/*
 * Wiring lock for lvdattach's numeric args (p1_tool.c cmd_lvdattach uses
 * ONLY this helper): img_type is parsed by p1_parse_u16 via
 * p1_parse_lvd_nums — hex img_type (0x82) must be accepted, and img_type
 * must be u16-ranged. sector stays u32; option_len is u16 (optional arg).
 */
static void test_lvd_nums(void)
{
    unsigned int s = 0, im = 0, op = 0;

    /* the exact live-run arguments that failed before the fix */
    CHECK(p1_parse_lvd_nums("4096", "0x82", "0x1C", &s, &im, &op) == 0 &&
          s == 4096u && im == 130u && op == 28u,
          "lvd_nums: 4096/0x82/0x1C -> 0, 4096, 130, 28");
    CHECK(p1_parse_lvd_nums("4096", "2", "28", &s, &im, &op) == 0 &&
          im == 2u && op == 28u,
          "lvd_nums: decimal 2/28 accepted");
    CHECK(p1_parse_lvd_nums("4096", "2", NULL, &s, &im, &op) == 0 && im == 2u && op == 28u,
          "lvd_nums: NULL opt_len leaves default (28) unchanged");
    CHECK(p1_parse_lvd_nums("4096", "0x1C", "0x1C", &s, &im, &op) == 0 && im == 28u,
          "lvd_nums: hex img_type accepted (0x1C -> 28)");
    CHECK(p1_parse_lvd_nums("4096", "0x82", NULL, &s, &im, &op) == 0 && im == 130u,
          "lvd_nums: hex img_type with default opt_len (0x82 -> 130)");
    CHECK(p1_parse_lvd_nums("4096", "65536", "28", &s, &im, &op) == 2,
          "lvd_nums: img_type overflow -> field 2 (img_type is u16)");
    CHECK(p1_parse_lvd_nums("4096", "2", "0x10000", &s, &im, &op) == 3,
          "lvd_nums: option_len overflow -> field 3");
    CHECK(p1_parse_lvd_nums("abc", "2", "28", &s, &im, &op) == 1,
          "lvd_nums: bad sector -> field 1");
    CHECK(p1_parse_lvd_nums(NULL, "2", "28", &s, &im, &op) == 1,
          "lvd_nums: NULL sector -> field 1");
}

/*
 * Layout locks for lvd_types.h — the REAL kstuff-lite types.h layout
 * (ps5-kstuff-ldr/include/types.h, p1-review-verdict.md:9). Host and target
 * are both x86-64, so these sizes/offsets are the target ABI too. The old
 * "simplified" tool.c struct fails every one of these (that was the review
 * defect: wrong ioctl layout => false-negative EINVAL).
 */
static void test_lvd_layout(void)
{
    CHECK(sizeof(lvd_kernel_layer_t) == 56, "lvd: sizeof(lvd_kernel_layer_t)==56");
    CHECK(offsetof(lvd_kernel_layer_t, source_type) == 0, "lvd: layer.source_type@0");
    CHECK(offsetof(lvd_kernel_layer_t, entry_flags) == 2, "lvd: layer.entry_flags@2");
    CHECK(offsetof(lvd_kernel_layer_t, path) == 8, "lvd: layer.path@8 (pointer)");
    CHECK(offsetof(lvd_kernel_layer_t, offset) == 16, "lvd: layer.offset@16");
    CHECK(offsetof(lvd_kernel_layer_t, size) == 24, "lvd: layer.size@24");
    CHECK(offsetof(lvd_kernel_layer_t, bitmap_path) == 32, "lvd: layer.bitmap_path@32");
    CHECK(offsetof(lvd_kernel_layer_t, bitmap_offset) == 40, "lvd: layer.bitmap_offset@40");
    CHECK(offsetof(lvd_kernel_layer_t, bitmap_size) == 48, "lvd: layer.bitmap_size@48");

    CHECK(sizeof(lvd_ioctl_attach_t) == 40, "lvd: sizeof(lvd_ioctl_attach_t)==40");
    CHECK(offsetof(lvd_ioctl_attach_t, io_version) == 0, "lvd: attach.io_version@0");
    CHECK(offsetof(lvd_ioctl_attach_t, device_id) == 4, "lvd: attach.device_id@4");
    CHECK(offsetof(lvd_ioctl_attach_t, sector_size_0) == 8, "lvd: attach.sector_size_0@8");
    CHECK(offsetof(lvd_ioctl_attach_t, sector_size_1) == 12, "lvd: attach.sector_size_1@12");
    CHECK(offsetof(lvd_ioctl_attach_t, option_len) == 16, "lvd: attach.option_len@16");
    CHECK(offsetof(lvd_ioctl_attach_t, image_type) == 18, "lvd: attach.image_type@18");
    CHECK(offsetof(lvd_ioctl_attach_t, layer_count) == 20, "lvd: attach.layer_count@20");
    CHECK(offsetof(lvd_ioctl_attach_t, device_size) == 24, "lvd: attach.device_size@24");
    CHECK(offsetof(lvd_ioctl_attach_t, layers_ptr) == 32, "lvd: attach.layers_ptr@32 (pointer)");

    CHECK(sizeof(lvd_ioctl_detach_t) == 40, "lvd: sizeof(lvd_ioctl_detach_t)==40");
    CHECK(offsetof(lvd_ioctl_detach_t, device_id) == 4, "lvd: detach.device_id@4");
    CHECK(offsetof(lvd_ioctl_detach_t, reserved) == 8, "lvd: detach.reserved@8");

    /* constants verbatim from types.h */
    CHECK(SCE_LVD_IOC_ATTACH == 0xC0286D00u, "lvd: SCE_LVD_IOC_ATTACH==0xC0286D00");
    CHECK(SCE_LVD_IOC_DETACH == 0xC0286D01u, "lvd: SCE_LVD_IOC_DETACH==0xC0286D01");

    /* encoding re-derivation: FreeBSD _IOWR('m', nr, 40-byte struct)
     * = 0xC0000000 | (size<<16) | (type<<8) | nr */
    CHECK((0xC0000000u | ((uint32_t)sizeof(lvd_ioctl_attach_t) << 16) |
           ((uint32_t)'m' << 8) | 0u) == SCE_LVD_IOC_ATTACH,
          "lvd: _IOWR('m',0,attach) == 0xC0286D00");
    CHECK((0xC0000000u | ((uint32_t)sizeof(lvd_ioctl_detach_t) << 16) |
           ((uint32_t)'m' << 8) | 1u) == SCE_LVD_IOC_DETACH,
          "lvd: _IOWR('m',1,detach) == 0xC0286D01");
}

/*
 * Script-line reader (script mode, p1.cmd). Covers: normal lines, blank
 * lines, comment lines, CRLF endings, long-line truncation, EOF.
 */
static FILE *mkscript(const char *s)
{
    FILE *f = tmpfile();
    if (f == NULL) {
        printf("FAIL: tmpfile() unavailable\n");
        failures++;
        return NULL;
    }
    fputs(s, f);
    rewind(f);
    return f;
}

static void test_script_next(void)
{
    char b[64];
    char t[8];
    FILE *f;

    CHECK(p1_script_next(NULL, b, sizeof(b)) == 0, "script: NULL fp -> 0");
    if ((f = mkscript("x\n")) != NULL) {
        CHECK(p1_script_next(f, NULL, 8) == 0, "script: NULL buf -> 0");
        CHECK(p1_script_next(f, b, 0) == 0, "script: n=0 -> 0");
        fclose(f);
    }

    if ((f = mkscript("")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 0, "script: empty file -> 0 (EOF)");
        fclose(f);
    }
    if ((f = mkscript("a\nb\n")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "a"), "script: line a");
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "b"), "script: line b");
        CHECK(p1_script_next(f, b, sizeof(b)) == 0, "script: EOF after b");
        fclose(f);
    }
    if ((f = mkscript("\n\nreal\n\n")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "real"), "script: blank lines skipped");
        CHECK(p1_script_next(f, b, sizeof(b)) == 0, "script: EOF after blanks");
        fclose(f);
    }
    if ((f = mkscript("   \t \nreal\n")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "real"), "script: ws-only line skipped");
        fclose(f);
    }
    if ((f = mkscript("# comment\nreal\n")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "real"), "script: # comment skipped");
        fclose(f);
    }
    if ((f = mkscript("  # indented\nx\n")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "x"), "script: indented # skipped");
        fclose(f);
    }
    if ((f = mkscript("#\nlast\n")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "last"), "script: bare # skipped");
        fclose(f);
    }
    if ((f = mkscript("echo # hi\n")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "echo # hi"), "script: mid-line # kept");
        fclose(f);
    }
    if ((f = mkscript("cmd arg\r\n")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "cmd arg"), "script: CRLF stripped");
        fclose(f);
    }
    if ((f = mkscript("\r\ny\r\n")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "y"), "script: CRLF blank skipped");
        fclose(f);
    }
    if ((f = mkscript("abcdefghij\nnext\n")) != NULL) {
        CHECK(p1_script_next(f, t, sizeof(t)) == 1 && !strcmp(t, "abcdefg"),
              "script: long line truncated to n-1");
        CHECK(p1_script_next(f, t, sizeof(t)) == 1 && !strcmp(t, "next"),
              "script: truncated tail dropped (no phantom line)");
        CHECK(p1_script_next(f, t, sizeof(t)) == 0, "script: EOF after truncated line");
        fclose(f);
    }
    if ((f = mkscript("abcdefg\nnext\n")) != NULL) {
        CHECK(p1_script_next(f, t, sizeof(t)) == 1 && !strcmp(t, "abcdefg"), "script: exact-fit line");
        CHECK(p1_script_next(f, t, sizeof(t)) == 1 && !strcmp(t, "next"), "script: line after exact-fit");
        fclose(f);
    }
    if ((f = mkscript("solo")) != NULL) {
        CHECK(p1_script_next(f, b, sizeof(b)) == 1 && !strcmp(b, "solo"), "script: last line w/o newline");
        CHECK(p1_script_next(f, b, sizeof(b)) == 0, "script: EOF after w/o-newline line");
        fclose(f);
    }
}

/*
 * `last` device token resolver + LVD path formatter (token substitution
 * used by mntfs source and lvddetach id).
 */
static void test_dev_token(void)
{
    int d = -99;
    char p[16];

    CHECK(p1_resolve_last("last", 3, &d) == 1 && d == 3, "last: resolves to 3");
    CHECK(p1_resolve_last("last", 0, &d) == 1 && d == 0, "last: resolves to 0");
    CHECK(p1_resolve_last("last", -1, &d) == -1, "last: none remembered -> -1");
    CHECK(p1_resolve_last("/dev/lvd3", 3, &d) == 0, "last: explicit path -> 0 (use as-is)");
    CHECK(p1_resolve_last("5", 3, &d) == 0, "last: numeric id -> 0 (use as-is)");
    CHECK(p1_resolve_last("Last", 3, &d) == 0, "last: case-sensitive");
    CHECK(p1_resolve_last("lastx", 3, &d) == 0, "last: prefix not matched");
    CHECK(p1_resolve_last("", 3, &d) == 0, "last: empty -> 0");
    CHECK(p1_resolve_last(NULL, 3, &d) == 0, "last: NULL -> 0");

    CHECK(p1_lvd_path(3, p, sizeof(p)) == 0 && !strcmp(p, "/dev/lvd3"), "lvd_path: 3 -> /dev/lvd3");
    CHECK(p1_lvd_path(0, p, sizeof(p)) == 0 && !strcmp(p, "/dev/lvd0"), "lvd_path: 0 -> /dev/lvd0");
    CHECK(p1_lvd_path(12, p, sizeof(p)) == 0 && !strcmp(p, "/dev/lvd12"), "lvd_path: 12 -> /dev/lvd12");
    CHECK(p1_lvd_path(-1, p, sizeof(p)) == -1, "lvd_path: negative -> -1");
    CHECK(p1_lvd_path(3, p, 4) == -1, "lvd_path: buffer too small -> -1");
}

/*
 * R8: `last_dev` is cleared after a SUCCESSFUL lvddetach (of `last` or an
 * explicit id that matches). After the clear, `lvddetach last` and
 * `mntfs ... last ...` must refuse ("ERR no last device") and issue NO
 * detach ioctl / NO nmount — p1_tool.c resolves the token and returns
 * before open("/dev/lvdctl")/nmount.
 */
static void test_r8_last_clear(void)
{
    int last = 3, dev = -1;

    CHECK(p1_last_after_detach(3, 3) == -1, "r8: detach of matching id clears last_dev");
    CHECK(p1_last_after_detach(3, 2) == 3, "r8: detach of other id keeps last_dev");
    CHECK(p1_last_after_detach(3, -1) == 3, "r8: detach of invalid id keeps last_dev");
    CHECK(p1_last_after_detach(-1, 3) == -1, "r8: already clear stays clear");

    /* full sequence: attach 3 -> detach `last` (success) -> refusals */
    last = p1_last_after_detach(last, last);
    CHECK(last == -1, "r8: successful lvddetach(last) clears");
    CHECK(p1_resolve_last("last", last, &dev) == -1,
          "r8: lvddetach last after clear -> refused (ERR no last device, no ioctl)");
    CHECK(p1_resolve_last("last", last, &dev) == -1,
          "r8: mntfs last after clear -> refused (ERR no last device, no nmount)");

    /* explicit detach of a matching id also clears */
    last = 3;
    last = p1_last_after_detach(last, 3);
    CHECK(last == -1, "r8: lvddetach 3 (matching) clears");
}

/*
 * Line sanitization before tokenizing (run_command's exact path: stdin mode
 * AND script mode). The live defect: fgets keeps the trailing '\n' and the
 * raw tokenizer splits only on space/tab, so it is glued to the LAST token.
 * Exact live lines from the failed run are the acceptance cases.
 */
static void test_tokenize_line(void)
{
    char live1[] = "lvdattach /mnt/usb0/jogos/pkg/EP3678-PPSA03706_00-2543889420204772.pkg 4096 2 0x1C\n";
    char live2[] = "mkdir /mnt/p1mnt\n";
    char live3[] = "lvdattach /mnt/usb0/jogos/pkg/EP3678-PPSA03706_00-2543889420204772.pkg 4096 2 0x1C\r\n";
    char live4[] = "ls /mnt\n";
    char *tok[16];
    int n;
    unsigned int v = 0;

    /* DEFECT DEMO — raw p1_tokenize is unchanged and keeps the '\n' */
    {
        char d[] = "lvdattach /a/b.pkg 4096 2 0x1C\n";
        n = p1_tokenize(d, tok, 16);
        CHECK(n == 5 && !strcmp(tok[4], "0x1C\n"),
              "defect demo: raw tokenize keeps \\n on last token (\"0x1C\\n\")");
    }
    {
        char d[] = "mkdir /mnt/p1mnt\n";
        n = p1_tokenize(d, tok, 16);
        CHECK(n == 2 && !strcmp(tok[1], "/mnt/p1mnt\n"),
              "defect demo: raw tokenize mkdir arg is \"/mnt/p1mnt\\n\" (created \"p1mnt\\n\")");
    }

    /* FIX — p1_tokenize_line (chomp + tokenize) on the EXACT live lines */
    n = p1_tokenize_line(live1, tok, 16);
    CHECK(n == 5, "live: lvdattach line -> 5 tokens");
    CHECK(n == 5 && !strcmp(tok[4], "0x1C"), "live: tok[4] == \"0x1C\" (no newline)");
    CHECK(n == 5 && p1_parse_u16(tok[4], &v) == 0 && v == 28u, "live: tok[4] parses to 28");
    n = p1_tokenize_line(live2, tok, 16);
    CHECK(n == 2 && !strcmp(tok[1], "/mnt/p1mnt"), "live: mkdir tok[1] == \"/mnt/p1mnt\"");
    n = p1_tokenize_line(live3, tok, 16);
    CHECK(n == 5 && !strcmp(tok[4], "0x1C"), "live CRLF: tok[4] == \"0x1C\"");
    n = p1_tokenize_line(live4, tok, 16);
    CHECK(n == 2 && !strcmp(tok[0], "ls") && !strcmp(tok[1], "/mnt"),
          "live: ls /mnt -> 2 clean tokens");
}

static void test_chomp(void)
{
    char a[] = "0x1C\n";
    char b[] = "/mnt/p1mnt\r\n";
    char c[] = "abc   \t\n";
    char d[] = "no-newline";
    char e[] = "\n";
    char f[] = "a\nb\n";

    CHECK(!strcmp(p1_chomp(a), "0x1C"), "chomp: strips \\n");
    CHECK(!strcmp(p1_chomp(b), "/mnt/p1mnt"), "chomp: strips \\r\\n");
    CHECK(!strcmp(p1_chomp(c), "abc"), "chomp: strips trailing spaces/tabs too");
    CHECK(!strcmp(p1_chomp(d), "no-newline"), "chomp: no line ending -> unchanged");
    CHECK(!strcmp(p1_chomp(e), ""), "chomp: only \\n -> empty");
    CHECK(!strcmp(p1_chomp(f), "a\nb"), "chomp: strips only the trailing end");
}

/*
 * Glob matcher for `rmdir <path>` leaf patterns (scratch cleanup of the
 * stray "/mnt/p1mnt\n" dir left by the tokenizer defect: shsrv strips
 * newlines and ftpsrv's line reader eats embedded LF, so the tool must
 * remove it via wildcard).
 */
static void test_glob_match(void)
{
    /* required cases */
    CHECK(p1_glob_match("p1mnt*", "p1mnt") == 1, "glob: p1mnt* ~ p1mnt");
    CHECK(p1_glob_match("p1mnt*", "p1mnt\n") == 1, "glob: p1mnt* ~ p1mnt\\n (stray dir)");
    CHECK(p1_glob_match("p1mnt*", "p1mntX") == 1, "glob: p1mnt* ~ p1mntX");
    CHECK(p1_glob_match("p1mn?", "p1mnt") == 1, "glob: p1mn? ~ p1mnt");
    CHECK(p1_glob_match("p1mnt", "p1mnt\n") == 0, "glob: p1mnt does NOT match p1mnt\\n");
    CHECK(p1_glob_match("p1mnt", "p1mnt") == 1, "glob: p1mnt ~ p1mnt (exact)");
    CHECK(p1_glob_match("*", "anything\n") == 1, "glob: * matches everything");
    CHECK(p1_glob_match("*", "") == 1, "glob: * matches empty");

    /* extras: '?' is exactly one char; star can match empty; anchors */
    CHECK(p1_glob_match("p1mn?", "p1mn") == 0, "glob: ? is exactly one char");
    CHECK(p1_glob_match("a*b", "ab") == 1, "glob: a*b ~ ab (star matches empty)");
    CHECK(p1_glob_match("a*b", "axxxb") == 1, "glob: a*b ~ axxxb");
    CHECK(p1_glob_match("a*b", "axbxc") == 0, "glob: a*b !~ axbxc (must end in b)");
    CHECK(p1_glob_match("*x*", "axc") == 1, "glob: *x* ~ axc");
    CHECK(p1_glob_match("*x*", "abc") == 0, "glob: *x* !~ abc");
    CHECK(p1_glob_match("", "") == 1, "glob: empty ~ empty");
    CHECK(p1_glob_match("", "x") == 0, "glob: empty !~ x");
    CHECK(p1_glob_match(NULL, "x") == 0, "glob: NULL pattern -> 0");
    CHECK(p1_glob_match("x", NULL) == 0, "glob: NULL name -> 0");
}

int main(void)
{
    test_tokenize();
    test_split_kv();
    test_parse_u32();
    test_parse_i32();
    test_parse_u16();
    test_lvd_nums();
    test_lvd_layout();
    test_script_next();
    test_dev_token();
    test_r8_last_clear();
    test_tokenize_line();
    test_chomp();
    test_glob_match();

    if (failures) {
        printf("\n%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("\nALL TESTS PASSED\n");
    return 0;
}
