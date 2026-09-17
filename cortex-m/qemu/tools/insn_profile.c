/*
 * QEMU TCG plugin: per-function dynamic instruction counts.
 *
 * Counts every executed guest instruction and attributes it to the ELF
 * symbol that contains the translation block. Used to profile h264bsd on
 * an emulated Cortex-M33 (QEMU does not model cycles, but on an in-order
 * single-issue core the dynamic instruction count is a good proxy).
 *
 * Build:  make -C cortex-m plugin   (needs glib-2.0 dev headers and
 *         include/qemu/qemu-plugin.h from the QEMU release you run)
 * Use:    qemu-system-arm ... -plugin ./libinsnprof.so[,top=40] -d plugin
 */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    char *name;
    uint64_t insns;
    uint64_t tbs;
} sym_count_t;

static GHashTable *syms;
static GMutex lock;
static uint64_t total_insns;
static int top_n = 40;

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    struct qemu_plugin_insn *first = qemu_plugin_tb_get_insn(tb, 0);
    const char *sym = qemu_plugin_insn_symbol(first);
    sym_count_t *e;

    if (!sym)
        sym = "<unknown>";

    g_mutex_lock(&lock);
    e = g_hash_table_lookup(syms, sym);
    if (!e) {
        e = g_new0(sym_count_t, 1);
        e->name = g_strdup(sym);
        g_hash_table_insert(syms, e->name, e);
    }
    g_mutex_unlock(&lock);

    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &e->insns, n);
    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &e->tbs, 1);
    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &total_insns, n);
}

static gint cmp_desc(gconstpointer a, gconstpointer b)
{
    const sym_count_t *x = a, *y = b;
    if (x->insns == y->insns) return 0;
    return x->insns < y->insns ? 1 : -1;
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    GList *l = g_list_sort(g_hash_table_get_values(syms), cmp_desc);
    GString *out = g_string_new(NULL);
    int i = 0;

    g_string_append_printf(out, "\n==== instruction profile: total %" PRIu64
                           " instructions ====\n", total_insns);
    g_string_append_printf(out, "%12s %7s %12s  %s\n", "insns", "%", "tbs",
                           "symbol");
    for (GList *it = l; it && i < top_n; it = it->next, i++) {
        sym_count_t *e = it->data;
        g_string_append_printf(out, "%12" PRIu64 " %6.2f%% %12" PRIu64 "  %s\n",
                               e->insns,
                               total_insns ? 100.0 * e->insns / total_insns : 0.0,
                               e->tbs, e->name);
    }
    qemu_plugin_outs(out->str);
    g_string_free(out, TRUE);
    g_list_free(l);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        if (g_str_has_prefix(argv[i], "top="))
            top_n = atoi(argv[i] + 4);
    }
    syms = g_hash_table_new(g_str_hash, g_str_equal);
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
