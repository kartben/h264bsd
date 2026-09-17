/*
 * QEMU TCG plugin: memory traffic and instruction mix profile.
 *
 * Counts, per address range given on the command line (range=name:start-end,
 * hex), the number of loads/stores and bytes moved, split by access size,
 * plus a simple cache simulation of each range (write-allocate, write-back,
 * LRU, configurable size/associativity/line) reporting misses and
 * write-backs. Also counts executed instructions by class (load, store,
 * branch, multiply, other) from the disassembly.
 *
 * Optional args: cache=<bytes>,line=<bytes>,ways=<n>, frame=<frame_marker_ignored>
 */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define MAX_RANGES 8
#define MAX_CFG 4

typedef struct {
    uint64_t tag;
    int valid, dirty;
    uint64_t lru;
} line_t;

typedef struct {
    uint64_t size, line, ways, sets;
    line_t *lines;
    uint64_t misses, writebacks, accesses;
} cache_t;

typedef struct {
    char name[32];
    uint64_t start, end;
    uint64_t ld_cnt[4], ld_bytes, st_cnt[4], st_bytes; /* by log2 size */
    cache_t cache[MAX_CFG];
} range_t;

static range_t ranges[MAX_RANGES];
static int nranges;
static range_t other;
static uint64_t cache_sizes[MAX_CFG] = {8192, 16384, 32768, 0};
static int ncfg = 3;
static uint64_t line_size = 32, cache_ways = 2;
static uint64_t lru_clock;

/* instruction classes */
enum { C_LOAD, C_STORE, C_BRANCH, C_MUL, C_OTHER, C_NUM };
static uint64_t class_count[C_NUM];
static const char *class_name[C_NUM] = {"load", "store", "branch", "multiply", "other"};

static void cache_init(cache_t *c, uint64_t size)
{
    c->size = size; c->line = line_size; c->ways = cache_ways;
    c->sets = size / (line_size * cache_ways);
    c->lines = g_new0(line_t, c->sets * c->ways);
}

static void cache_access(cache_t *c, uint64_t addr, int is_store)
{
    uint64_t lineaddr = addr / c->line;
    uint64_t set = lineaddr % c->sets;
    line_t *ways = c->lines + set * c->ways;
    uint64_t i, victim = 0;
    c->accesses++;
    for (i = 0; i < c->ways; i++) {
        if (ways[i].valid && ways[i].tag == lineaddr) {
            ways[i].lru = ++lru_clock;
            if (is_store) ways[i].dirty = 1;
            return;
        }
    }
    c->misses++;
    for (i = 1; i < c->ways; i++)
        if (!ways[i].valid || ways[i].lru < ways[victim].lru) victim = i;
    if (ways[victim].valid && ways[victim].dirty) c->writebacks++;
    ways[victim].valid = 1; ways[victim].dirty = is_store;
    ways[victim].tag = lineaddr; ways[victim].lru = ++lru_clock;
}

static void vcpu_mem(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                     uint64_t vaddr, void *udata)
{
    int is_store = qemu_plugin_mem_is_store(info);
    unsigned shift = qemu_plugin_mem_size_shift(info);
    uint64_t bytes = 1ull << shift;
    range_t *r = &other;
    int i;
    for (i = 0; i < nranges; i++)
        if (vaddr >= ranges[i].start && vaddr < ranges[i].end) { r = &ranges[i]; break; }
    if (shift > 3) shift = 3;
    if (is_store) { r->st_cnt[shift]++; r->st_bytes += bytes; }
    else { r->ld_cnt[shift]++; r->ld_bytes += bytes; }
    if (r != &other) {
        /* an access crossing a line boundary touches two lines */
        uint64_t last = vaddr + bytes - 1;
        for (i = 0; i < ncfg; i++) {
            cache_access(&r->cache[i], vaddr, is_store);
            if (last / line_size != vaddr / line_size)
                cache_access(&r->cache[i], last, is_store);
        }
    }
}

/* Classify a Thumb/Thumb-2 instruction from its encoding (the plugin
 * disassembler is not available in every QEMU build). */
static int classify_bytes(const uint8_t *b, size_t size)
{
    uint16_t hw1 = b[0] | (b[1] << 8);
    if (size == 2) {
        uint16_t op = hw1 >> 11;                     /* top 5 bits */
        if ((hw1 & 0xFE00) == 0x5000) return (((hw1 >> 9) & 7) < 3) ? C_STORE : C_LOAD;
        if (op >= 0x0C && op <= 0x13) return (hw1 & 0x0800) ? C_LOAD : C_STORE;  /* imm ld/st, ldrh/strh, sp-rel */
        if (op == 0x09) return C_LOAD;                                            /* ldr literal */
        if ((hw1 & 0xFE00) == 0xB400) return C_STORE;                             /* push */
        if ((hw1 & 0xFE00) == 0xBC00) return C_LOAD;                              /* pop */
        if ((hw1 & 0xF800) == 0xC000) return C_STORE;                             /* stm */
        if ((hw1 & 0xF800) == 0xC800) return C_LOAD;                              /* ldm */
        if ((hw1 & 0xF000) == 0xD000 && (hw1 & 0x0F00) != 0x0E00 && (hw1 & 0x0F00) != 0x0F00) return C_BRANCH; /* b<cond> */
        if ((hw1 & 0xF800) == 0xE000) return C_BRANCH;                            /* b */
        if ((hw1 & 0xF500) == 0xB100) return C_BRANCH;                            /* cbz/cbnz */
        if ((hw1 & 0xFF00) == 0x4700) return C_BRANCH;                            /* bx/blx */
        if ((hw1 & 0xFFC0) == 0x4340) return C_MUL;                               /* muls */
        return C_OTHER;
    } else {
        uint16_t hw2 = b[2] | (b[3] << 8);
        if ((hw1 & 0xF800) == 0xF000 && (hw2 & 0x8000)) return C_BRANCH;         /* b.w, bl, blx */
        if ((hw1 & 0xFFF0) == 0xE8D0 && (hw2 & 0xFFE0) == 0xF000) return C_BRANCH; /* tbb/tbh */
        if ((hw1 & 0xFE00) == 0xE800) return (hw1 & 0x0010) ? C_LOAD : C_STORE;  /* ldm/stm/ldrd/strd/ldrex */
        if ((hw1 & 0xFE00) == 0xF800) return (hw1 & 0x0010) ? C_LOAD : C_STORE;  /* ldr/str single */
        if ((hw1 & 0xFF00) == 0xFB00) return C_MUL;                              /* mul/mla/smlad/smuad/... */
        if ((hw1 & 0xFF00) == 0xFB80 || (hw1 & 0xFF00) == 0xFB00) return C_MUL;  /* long multiply / divide */
        if ((hw1 & 0xFE00) == 0xEC00) return (hw1 & 0x0010) ? C_LOAD : C_STORE;  /* vldr/vstr etc. */
        return C_OTHER;
    }
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb), i;
    uint64_t counts[C_NUM] = {0};
    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        counts[classify_bytes(qemu_plugin_insn_data(insn), qemu_plugin_insn_size(insn))]++;
        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem, QEMU_PLUGIN_CB_NO_REGS,
                                         QEMU_PLUGIN_MEM_RW, NULL);
    }
    for (i = 0; i < C_NUM; i++)
        if (counts[i])
            qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                     &class_count[i], counts[i]);
}

static void print_range(GString *out, range_t *r)
{
    int i;
    g_string_append_printf(out, "range %-14s loads: %" PRIu64 " (b=%" PRIu64 " h=%" PRIu64 " w=%" PRIu64 " d=%" PRIu64 ") %" PRIu64 " bytes; "
        "stores: %" PRIu64 " (b=%" PRIu64 " h=%" PRIu64 " w=%" PRIu64 " d=%" PRIu64 ") %" PRIu64 " bytes\n",
        r->name, r->ld_cnt[0]+r->ld_cnt[1]+r->ld_cnt[2]+r->ld_cnt[3], r->ld_cnt[0], r->ld_cnt[1], r->ld_cnt[2], r->ld_cnt[3], r->ld_bytes,
        r->st_cnt[0]+r->st_cnt[1]+r->st_cnt[2]+r->st_cnt[3], r->st_cnt[0], r->st_cnt[1], r->st_cnt[2], r->st_cnt[3], r->st_bytes);
    if (r == &other) return;
    for (i = 0; i < ncfg; i++)
        g_string_append_printf(out, "  cache %6" PRIu64 " B %" PRIu64 "-way %" PRIu64 "B lines: misses %" PRIu64 " writebacks %" PRIu64 " (of %" PRIu64 " accesses)\n",
            r->cache[i].size, r->cache[i].ways, r->cache[i].line, r->cache[i].misses, r->cache[i].writebacks, r->cache[i].accesses);
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    GString *out = g_string_new("\n==== memory / instruction mix profile ====\n");
    uint64_t total = 0;
    int i;
    for (i = 0; i < C_NUM; i++) total += class_count[i];
    g_string_append_printf(out, "instructions: %" PRIu64 "\n", total);
    for (i = 0; i < C_NUM; i++)
        g_string_append_printf(out, "  %-9s %12" PRIu64 " (%.1f%%)\n", class_name[i], class_count[i], total ? 100.0 * class_count[i] / total : 0.0);
    for (i = 0; i < nranges; i++) print_range(out, &ranges[i]);
    strcpy(other.name, "other");
    print_range(out, &other);
    qemu_plugin_outs(out->str);
    g_string_free(out, TRUE);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                                           int argc, char **argv)
{
    int i, j;
    for (i = 0; i < argc; i++) {
        if (g_str_has_prefix(argv[i], "range=") && nranges < MAX_RANGES) {
            char name[32]; unsigned long long s, e;
            if (sscanf(argv[i] + 6, "%31[^:]:%llx-%llx", name, &s, &e) == 3) {
                strcpy(ranges[nranges].name, name);
                ranges[nranges].start = s; ranges[nranges].end = e;
                nranges++;
            }
        } else if (g_str_has_prefix(argv[i], "line=")) line_size = atoi(argv[i] + 5);
        else if (g_str_has_prefix(argv[i], "ways=")) cache_ways = atoi(argv[i] + 5);
    }
    for (i = 0; i < nranges; i++)
        for (j = 0; j < ncfg; j++) cache_init(&ranges[i].cache[j], cache_sizes[j]);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
