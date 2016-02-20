/* This is a generated file. */
#ifndef CONFIG_H_
#define CONFIG_H_

/* Probe byte-order via defines (clang & gcc at least work) to avoid run-time
 * tests */
#ifdef __BYTE_ORDER__
# ifdef __ORDER_LITTLE_ENDIAN__
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#   define HAVE_LITTLE_ENDIAN 1
#  endif
# endif
# ifdef __ORDER_BIG_ENDIAN__
#  if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#   define HAVE_BIG_ENDIAN 1
#  endif
# endif
#endif


#ifndef HAVE_LITTLE_ENDIAN
# ifdef HAVE_BIG_ENDIAN
#  define HAVE_LITTLE_ENDIAN 0
# else
/* tcc is mean and doesn't give us any hints. Thankfully, it only supports LE */
#  ifdef __TINYC__
#   define HAVE_LITTLE_ENDIAN 1
#   define HAVE_BIG_ENDIAN 0
#  else
#   warning "No endian detected, try expanding tests"
#   define HAVE_LITTLE_ENDIAN 0
#   define HAVE_BIG_ENDIAN 0
#  endif
# endif
#else
# ifndef HAVE_BIG_ENDIAN
#  define HAVE_BIG_ENDIAN 0
# else
#  error "Unexpected"
# endif
#endif
