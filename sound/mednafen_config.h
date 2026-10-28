#if defined(__x86_64__)
#define ARCH_X86 1
#define ARCH_X86_64 1
#elif defined(__i386__)
#define ARCH_X86 1
#define ARCH_X86_32 1
#endif
#define HAVE_ALLOCA 1
#define HAVE_ALLOCA_H 1
#define HAVE_STDINT_H 1
#define HAVE_SYS_TYPES_H 1
#define LSB_FIRST 1
#define MDFN_PSS_STYLE 1
#define SIZEOF_CHAR 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG __SIZEOF_LONG__
#define SIZEOF_LONG_LONG 8
#define SIZEOF_OFF_T 8
#define SIZEOF_PTRDIFF_T __SIZEOF_POINTER__
#define SIZEOF_SIZE_T __SIZEOF_POINTER__
#define SIZEOF_VOID_P __SIZEOF_POINTER__
#define SIZEOF_DOUBLE 8
#define SIZEOF___INT128 0
