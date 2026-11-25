/* GOT / function-pointer slot hooking by symbol name.
 *
 * Finds the loaded game library via dl_iterate_phdr, resolves each wanted symbol through DT_HASH,
 * then scans JUMP_SLOT / GLOB_DAT / ABS64 relocations for slots that reference it. Every slot is
 * validated (it must currently hold the symbol's address) before it is patched. Hooks are grouped:
 * if any required member of a group fails validation, the whole group is skipped so related hooks
 * are never half-installed. */
#include "rt.h"

#define PT_DYNAMIC 2
#define PT_GNU_RELRO 0x6474e552
#define DT_NULL 0
#define DT_PLTRELSZ 2
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_JMPREL 23
#define R_ABS64 257
#define R_GLOB_DAT 1025
#define R_JUMP_SLOT 1026

typedef struct { uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align; } ElfPhdr;
typedef struct { int64_t d_tag; uint64_t d_val; } ElfDyn;
typedef struct { uint32_t st_name; uint8_t st_info, st_other; uint16_t st_shndx; uint64_t st_value, st_size; } ElfSym;
typedef struct { uint64_t r_offset, r_info; int64_t r_addend; } ElfRela;

typedef struct {
    const char *symbol;
    void *replacement;
    void **original;
    uint8_t group;      /* 0 = standalone */
    uint8_t optional;   /* missing/invalid does not disable the group */
    /* filled during install */
    int32_t sym_index;
    uintptr_t target;
    uint16_t slots;
    uint8_t valid;
    uint8_t installed;
} HookDef;

/* H(group, "symbol", replacement, original-pointer-variable) */
#define H(group, sym, hook, orig) { sym, (void *)&(hook), (void **)&(orig), group, 0, 0, 0, 0, 0, 0 }

typedef struct {
    const char *suffix;
    uintptr_t base;
    const char *path;
    const ElfSym *symtab;
    const char *strtab;
    const uint32_t *hash;
    const ElfRela *jmprel;
    size_t jmprel_count;
    const ElfRela *rela;
    size_t rela_count;
    uintptr_t relro_start, relro_end;
} ElfModule;

static uintptr_t dyn_address(const ElfModule *m, uint64_t value) {
    return value < m->base ? m->base + (uintptr_t)value : (uintptr_t)value;
}

static int module_callback(struct dl_phdr_info_rt *info, size_t size, void *data) {
    ElfModule *m = data;
    const char *name = info->dlpi_name;
    if (!name) return 0;
    size_t n = strlen(name), k = strlen(m->suffix);
    if (n < k || strcmp(name + n - k, m->suffix) != 0 || (n > k && name[n - k - 1] != '/')) return 0;
    m->base = info->dlpi_addr;
    m->path = name;
    const ElfPhdr *ph = info->dlpi_phdr;
    const ElfDyn *dyn = NULL;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC) dyn = (const ElfDyn *)(m->base + ph[i].p_vaddr);
        if (ph[i].p_type == PT_GNU_RELRO) {
            m->relro_start = m->base + ph[i].p_vaddr;
            m->relro_end = m->relro_start + ph[i].p_memsz;
        }
    }
    if (!dyn) return 1;
    uint64_t pltrelsz = 0, relasz = 0;
    for (const ElfDyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB: m->symtab = (const ElfSym *)dyn_address(m, d->d_val); break;
            case DT_STRTAB: m->strtab = (const char *)dyn_address(m, d->d_val); break;
            case DT_HASH: m->hash = (const uint32_t *)dyn_address(m, d->d_val); break;
            case DT_JMPREL: m->jmprel = (const ElfRela *)dyn_address(m, d->d_val); break;
            case DT_PLTRELSZ: pltrelsz = d->d_val; break;
            case DT_RELA: m->rela = (const ElfRela *)dyn_address(m, d->d_val); break;
            case DT_RELASZ: relasz = d->d_val; break;
            default: break;
        }
    }
    m->jmprel_count = (size_t)(pltrelsz / sizeof(ElfRela));
    m->rela_count = (size_t)(relasz / sizeof(ElfRela));
    return 1;
}

static uint32_t elf_hash(const char *name) {
    uint32_t h = 0, g;
    while (*name) {
        h = (h << 4) + (uint8_t)*name++;
        g = h & 0xf0000000u;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

static int32_t elf_find_symbol(const ElfModule *m, const char *name) {
    if (!m->hash || !m->symtab || !m->strtab) return -1;
    uint32_t nbucket = m->hash[0], nchain = m->hash[1];
    if (!nbucket) return -1;
    const uint32_t *bucket = m->hash + 2, *chain = bucket + nbucket;
    for (uint32_t i = bucket[elf_hash(name) % nbucket]; i && i < nchain; i = chain[i])
        if (strcmp(m->strtab + m->symtab[i].st_name, name) == 0) return (int32_t)i;
    return -1;
}

static int elf_open(ElfModule *m, const char *suffix) {
    memset(m, 0, sizeof(*m));
    m->suffix = suffix;
    dl_iterate_phdr(module_callback, m);
    return m->base && m->symtab && m->strtab && m->hash && m->jmprel;
}

/* Address of an exported symbol inside the module (0 if missing or undefined). */
static uintptr_t elf_symbol_address(const ElfModule *m, const char *name) {
    int32_t i = elf_find_symbol(m, name);
    if (i <= 0 || !m->symtab[i].st_shndx || !m->symtab[i].st_value) return 0;
    return m->base + (uintptr_t)m->symtab[i].st_value;
}

typedef struct { uintptr_t slot; uint16_t hook; } SlotPatch;
static SlotPatch hook_patches[512];
static int hook_patch_count;

static void collect_slots(const ElfModule *m, HookDef *hooks, int count, const ElfRela *table, size_t entries) {
    for (size_t r = 0; r < entries; r++) {
        uint32_t type = (uint32_t)table[r].r_info;
        uint32_t si = (uint32_t)(table[r].r_info >> 32);
        if (!si || (type != R_JUMP_SLOT && type != R_GLOB_DAT && type != R_ABS64)) continue;
        for (int h = 0; h < count; h++) {
            if (hooks[h].sym_index != (int32_t)si || !hooks[h].target) continue;
            uintptr_t slot = m->base + (uintptr_t)table[r].r_offset;
            uintptr_t expected = hooks[h].target + (type == R_ABS64 ? (uintptr_t)table[r].r_addend : 0);
            uintptr_t current = __atomic_load_n((uintptr_t *)slot, __ATOMIC_ACQUIRE);
            if (current != expected) {
                LOGW("hook %s: slot %p holds %p, expected %p - skipped", hooks[h].symbol, (void *)slot,
                     (void *)current, (void *)expected);
                continue;
            }
            if (hook_patch_count < (int)COUNTOF(hook_patches)) {
                hook_patches[hook_patch_count].slot = slot;
                hook_patches[hook_patch_count].hook = (uint16_t)h;
                hook_patch_count++;
                hooks[h].slots++;
            }
        }
    }
}

static int write_slot(const ElfModule *m, uintptr_t slot, void *value) {
    int in_relro = slot >= m->relro_start && slot < m->relro_end;
    if (in_relro) {
        uintptr_t page = slot & ~((uintptr_t)getpagesize() - 1);
        if (mprotect((void *)page, (size_t)getpagesize(), PROT_READ | PROT_WRITE) != 0) return 0;
        __atomic_store_n((void **)slot, value, __ATOMIC_RELEASE);
        mprotect((void *)page, (size_t)getpagesize(), PROT_READ);
    } else {
        __atomic_store_n((void **)slot, value, __ATOMIC_RELEASE);
    }
    return 1;
}

/* Returns the number of patched slots; fills per-hook status. */
static int hooks_install(const ElfModule *m, HookDef *hooks, int count) {
    hook_patch_count = 0;
    for (int h = 0; h < count; h++) {
        hooks[h].sym_index = elf_find_symbol(m, hooks[h].symbol);
        hooks[h].target = hooks[h].sym_index > 0 ? elf_symbol_address(m, hooks[h].symbol) : 0;
        hooks[h].slots = 0;
        hooks[h].valid = 0;
        hooks[h].installed = 0;
        if (!hooks[h].target) LOGW("hook %s: symbol not found", hooks[h].symbol);
    }
    collect_slots(m, hooks, count, m->jmprel, m->jmprel_count);
    collect_slots(m, hooks, count, m->rela, m->rela_count);
    uint8_t group_ok[256];
    memset(group_ok, 1, sizeof(group_ok));
    for (int h = 0; h < count; h++) {
        hooks[h].valid = hooks[h].target && hooks[h].slots > 0;
        if (!hooks[h].valid && !hooks[h].optional && hooks[h].group) group_ok[hooks[h].group] = 0;
    }
    for (int h = 0; h < count; h++) {
        if (hooks[h].valid && hooks[h].group && !group_ok[hooks[h].group]) {
            LOGW("hook %s: group %d incomplete - skipped", hooks[h].symbol, hooks[h].group);
            hooks[h].valid = 0;
        }
        if (hooks[h].valid) *hooks[h].original = (void *)hooks[h].target;
    }
    int patched = 0;
    for (int p = 0; p < hook_patch_count; p++) {
        HookDef *hook = &hooks[hook_patches[p].hook];
        if (!hook->valid) continue;
        if (write_slot(m, hook_patches[p].slot, hook->replacement)) {
            hook->installed = 1;
            patched++;
        } else {
            LOGE("hook %s: mprotect failed for slot %p", hook->symbol, (void *)hook_patches[p].slot);
        }
    }
    return patched;
}
