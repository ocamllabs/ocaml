#if __STDC_VERSION__ >= 201112L
#warning "You seem to have a C11 compiler. Someone should implement atomic.h in terms of C11 primitives..."
#endif

/* At least 32 bits, and big enough to store a pointer */
typedef unsigned long long atomic_word;

/* CPU_RELAX() should be called inside spin-wait loops */
#if defined(__x86_64__) || defined(__i386__)
#define CPU_RELAX() asm volatile("pause" ::: "memory")
/* #define CPU_RELAX() __builtin_ia32_pause()  on later GCC versions*/ 
#else
#define CPU_RELAX()
#endif


/* Put this in a structure to separate parts into different cache lines.
   This is wasteful of space; don't do it for common structures */
#define GENSYM_CACHE_LINE_SEPARATOR_2(line) cache_line_pad##line
#define GENSYM_CACHE_LINE_SEPARATOR_1(line) GENSYM_CACHE_LINE_SEPARATOR_2(line)
#define CACHE_LINE_SEPARATOR char GENSYM_CACHE_LINE_SEPARATOR_1(__LINE__)[128]


/* Load acquire and store release */

#if defined(__x86_64__) || defined(__i386__)

/* On x86 and x86-64, all loads are acquire and all stores are
   release. We need only prevent the compiler from reordering ops */
#if defined(__GNUC__)
#define COMPILER_BARRIER() __asm__ __volatile__ ("" ::: "memory")
#else
#error "COMPILER_BARRIER not defined for this compiler"
#endif

static atomic_word load_acquire(atomic_word* w) {
  atomic_word val = *(volatile atomic_word*)w;
  COMPILER_BARRIER();
  return val;
}

static void store_release(atomic_word* w, atomic_word val) {
  COMPILER_BARRIER();
  *(volatile atomic_word*)w = val;
}

#else
#error "unsupported platform"
#endif

/* Spin until a value is initialised and then return it. Has acquire semantics. */
static atomic_word load_wait(atomic_word* w) {
  while (1) {
    atomic_word val = load_acquire(w);
    if (val) {
      return val;
    }
    CPU_RELAX();
  }
}

/* Pointer versions of load/store functions */
static void* load_acquire_ptr(atomic_word* w) { return (void*)load_acquire(w); }
static void store_release_ptr(atomic_word* w, void* v) { store_release(w, (atomic_word)v); }
static void* load_wait_ptr(atomic_word* w) { return (void*)load_wait(w); }


/* Atomic read-modify-write operations. All of these have full barriers */

#if defined(__GNUC__)

/* atomically { return *w++; } */
static atomic_word fetch_and_increment(atomic_word* w) {
  return __sync_fetch_and_add(w, 1);
}

/* atomically { return *w += n; } */
static atomic_word fetch_and_add(atomic_word* w, atomic_word n) {
  return __sync_fetch_and_add(w, n);
}

/* atomically { if (*w == old) { *w = new; return 1; } else { return 0; } } */
static int cas_transition(atomic_word* w, atomic_word old, atomic_word new) {
  return __sync_bool_compare_and_swap(w, old, new);
}

/* atomically { atomic_word prev = *w; if (prev == old) { *w = new; }; return prev; } */
static int compare_and_swap(atomic_word* w, atomic_word old, atomic_word new) {
  return __sync_val_compare_and_swap(w, old, new);
}

#else
#error "unsupported platform"
#endif

