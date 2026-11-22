/*
 * MiSTerFiles (GroovyArcade build): glibc compatibility shims.
 *
 * The GroovyArcade binary is linked against a glibc 2.28 sysroot so it runs on old and new
 * x86_64 distros alike, but the static libstdc++/libgcc that ship with the build compiler were
 * compiled against a newer glibc and reference a few symbols that glibc 2.28 does not export.
 * They are provided here, implemented only with interfaces glibc 2.28 already has.
 * Only linked into the GroovyArcade target.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <link.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/random.h>
#include <sys/stat.h>

/* glibc 2.32: libstdc++ skips atomic refcounting when this is non-zero. 0 = always use atomics (safe). */
char __libc_single_threaded = 0;

/* glibc 2.36: used by std::random_device */
uint32_t arc4random(void)
{
   uint32_t v;
   ssize_t n;
   do n = getrandom(&v, sizeof v, 0); while (n < 0 && errno == EINTR);
   if (n != (ssize_t)sizeof v) { v = (uint32_t)rand() ^ ((uint32_t)rand() << 16); }
   return v;
}

/* glibc 2.38: C23 versions of strto* (only differ in accepting "0b" binary prefixes) */
long __isoc23_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
unsigned long __isoc23_strtoul(const char *s, char **e, int b) { return strtoul(s, e, b); }
long long __isoc23_strtoll(const char *s, char **e, int b) { return strtoll(s, e, b); }
unsigned long long __isoc23_strtoull(const char *s, char **e, int b) { return strtoull(s, e, b); }
int __isoc23_sscanf(const char *s, const char *f, ...)
{
   va_list ap; va_start(ap, f); int r = vsscanf(s, f, ap); va_end(ap); return r;
}
int __isoc23_vsscanf(const char *s, const char *f, va_list ap) { return vsscanf(s, f, ap); }

/* glibc 2.33 exports stat/fstat/lstat directly (2.28 only has __xstat & co.) */
int fstat(int fd, struct stat *st) { return __fxstat(_STAT_VER, fd, st); }
int stat(const char *p, struct stat *st) { return __xstat(_STAT_VER, p, st); }
int lstat(const char *p, struct stat *st) { return __lxstat(_STAT_VER, p, st); }

/* glibc 2.35: _dl_find_object, used by the libgcc unwinder (C++ exceptions).
   Same result built on dl_iterate_phdr (what libgcc used before 2.35). */
struct dl_find_object
{
   unsigned long long dlfo_flags;
   void *dlfo_map_start;
   void *dlfo_map_end;
   struct link_map *dlfo_link;
   void *dlfo_eh_frame;
   unsigned long long __dflo_reserved[7];
};

struct find_ctx { uintptr_t pc; struct dl_find_object *out; int found; };

static int find_cb(struct dl_phdr_info *info, size_t size, void *data)
{
   (void)size;
   struct find_ctx *c = (struct find_ctx *)data;
   uintptr_t lo = UINTPTR_MAX, hi = 0;
   void *eh = NULL;
   int inside = 0;
   for (int i = 0; i < info->dlpi_phnum; ++i)
   {
      const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
      if (ph->p_type == PT_LOAD)
      {
         uintptr_t s = info->dlpi_addr + ph->p_vaddr, e = s + ph->p_memsz;
         if (s < lo) lo = s;
         if (e > hi) hi = e;
         if (c->pc >= s && c->pc < e) inside = 1;
      }
      else if (ph->p_type == PT_GNU_EH_FRAME)
         eh = (void *)(info->dlpi_addr + ph->p_vaddr);
   }
   if (!inside) return 0;
   memset(c->out, 0, sizeof *c->out);
   c->out->dlfo_map_start = (void *)lo;
   c->out->dlfo_map_end = (void *)hi;
   c->out->dlfo_eh_frame = eh;
   c->found = 1;
   return 1;
}

int _dl_find_object(void *pc, struct dl_find_object *result)
{
   struct find_ctx c = { (uintptr_t)pc, result, 0 };
   dl_iterate_phdr(find_cb, &c);
   return c.found ? 0 : -1;
}
