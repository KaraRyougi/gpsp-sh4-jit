#include "fxcg100_platform.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef CGBA_DEBUG_PORT
#define CGBA_DEBUG_PORT 0
#endif

extern unsigned char __heap_start[];
extern unsigned char __heap_end[];

typedef struct cgba_heap_header {
  size_t size;
} cgba_heap_header;

static unsigned char *heap_cursor;

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t alignment)
{
  return (value + alignment - 1) & ~(alignment - 1);
}

void fxcg100_debug_char(int c)
{
#if CGBA_DEBUG_PORT
  *(volatile uint8_t *)0xfffff000 = (uint8_t)c;
#else
  (void)c;
#endif
}

int putchar(int c)
{
  if (c == '\n')
    fxcg100_debug_char('\r');
  fxcg100_debug_char(c);
  return c;
}

void fxcg100_debug_puts(const char *text)
{
  if (!text)
    text = "(null)";

  while (*text)
    putchar((unsigned char)*text++);
}

int puts(const char *text)
{
  fxcg100_debug_puts(text);
  putchar('\n');
  return 0;
}

void fxcg100_debug_hex32(uint32_t value)
{
  static const char hex[] = "0123456789abcdef";
  int i;

  for (i = 7; i >= 0; i--)
    putchar(hex[(value >> (i * 4)) & 0xf]);
}

static void out_repeat(void (*out)(char, void *), void *ctx, char ch, int count)
{
  while (count-- > 0)
    out(ch, ctx);
}

static int number_len(uint64_t value, unsigned base)
{
  int len = 1;
  while (value >= base) {
    value /= base;
    len++;
  }
  return len;
}

static void out_uint(void (*out)(char, void *), void *ctx, uint64_t value,
                     unsigned base, int width, int zero_pad, int upper)
{
  char tmp[32];
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  int len = number_len(value, base);
  int pos = 0;

  out_repeat(out, ctx, zero_pad ? '0' : ' ', width - len);

  do {
    tmp[pos++] = digits[value % base];
    value /= base;
  } while (value);

  while (pos)
    out(tmp[--pos], ctx);
}

static void debug_out(char ch, void *ctx)
{
  (void)ctx;
  putchar((unsigned char)ch);
}

typedef struct buffer_out_ctx {
  char *buffer;
  size_t capacity;
  size_t length;
} buffer_out_ctx;

static void buffer_out(char ch, void *opaque)
{
  buffer_out_ctx *ctx = (buffer_out_ctx *)opaque;

  if (ctx->length + 1 < ctx->capacity)
    ctx->buffer[ctx->length] = ch;
  ctx->length++;
}

static int vformat(void (*out)(char, void *), void *ctx,
                   const char *fmt, va_list ap)
{
  int written = 0;

  while (*fmt) {
    int width = 0;
    int zero_pad = 0;
    int long_arg = 0;
    char spec;

    if (*fmt != '%') {
      out(*fmt++, ctx);
      written++;
      continue;
    }

    fmt++;
    if (*fmt == '%') {
      out(*fmt++, ctx);
      written++;
      continue;
    }

    if (*fmt == '0') {
      zero_pad = 1;
      fmt++;
    }

    while (*fmt >= '0' && *fmt <= '9') {
      width = width * 10 + (*fmt - '0');
      fmt++;
    }

    if (*fmt == 'l') {
      long_arg = 1;
      fmt++;
      if (*fmt == 'l')
        fmt++;
    }

    spec = *fmt ? *fmt++ : '\0';

    switch (spec) {
    case 'c': {
      int c = va_arg(ap, int);
      out((char)c, ctx);
      written++;
      break;
    }
    case 's': {
      const char *s = va_arg(ap, const char *);
      int len = 0;
      if (!s)
        s = "(null)";
      while (s[len])
        len++;
      out_repeat(out, ctx, ' ', width - len);
      while (*s) {
        out(*s++, ctx);
        written++;
      }
      break;
    }
    case 'd':
    case 'i': {
      int64_t value = long_arg ? va_arg(ap, long) : va_arg(ap, int);
      uint64_t mag;
      if (value < 0) {
        out('-', ctx);
        written++;
        mag = (uint64_t)(-value);
      } else {
        mag = (uint64_t)value;
      }
      out_uint(out, ctx, mag, 10, width, zero_pad, 0);
      break;
    }
    case 'u':
      out_uint(out, ctx,
               long_arg ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int),
               10, width, zero_pad, 0);
      break;
    case 'x':
    case 'X':
      out_uint(out, ctx,
               long_arg ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int),
               16, width, zero_pad, spec == 'X');
      break;
    case 'p':
      out('0', ctx);
      out('x', ctx);
      out_uint(out, ctx, (uintptr_t)va_arg(ap, void *), 16,
               sizeof(void *) * 2, 1, 0);
      break;
    default:
      out('%', ctx);
      if (spec)
        out(spec, ctx);
      break;
    }
  }

  return written;
}

int vprintf(const char *fmt, va_list ap)
{
  return vformat(debug_out, NULL, fmt, ap);
}

int printf(const char *fmt, ...)
{
  int ret;
  va_list ap;

  va_start(ap, fmt);
  ret = vprintf(fmt, ap);
  va_end(ap);
  return ret;
}

int vsnprintf(char *buffer, size_t size, const char *fmt, va_list ap)
{
  buffer_out_ctx ctx;

  ctx.buffer = buffer;
  ctx.capacity = size;
  ctx.length = 0;

  vformat(buffer_out, &ctx, fmt, ap);

  if (size) {
    size_t nul = ctx.length < size ? ctx.length : size - 1;
    buffer[nul] = '\0';
  }

  return (int)ctx.length;
}

int snprintf(char *buffer, size_t size, const char *fmt, ...)
{
  int ret;
  va_list ap;

  va_start(ap, fmt);
  ret = vsnprintf(buffer, size, fmt, ap);
  va_end(ap);
  return ret;
}

void *malloc(size_t size)
{
  cgba_heap_header *header;
  uintptr_t cursor;
  uintptr_t end;

  if (!heap_cursor)
    heap_cursor = (unsigned char *)align_up_uintptr((uintptr_t)__heap_start, 8);

  size = (size + 7) & ~(size_t)7;
  cursor = align_up_uintptr((uintptr_t)heap_cursor, 8);
  end = cursor + sizeof(cgba_heap_header) + size;

  if (end > (uintptr_t)__heap_end)
    return NULL;

  header = (cgba_heap_header *)cursor;
  header->size = size;
  heap_cursor = (unsigned char *)end;
  return (void *)(header + 1);
}

void free(void *ptr)
{
  (void)ptr;
}

void *calloc(size_t nmemb, size_t size)
{
  size_t total = nmemb * size;
  void *ptr = malloc(total);
  if (ptr)
    memset(ptr, 0, total);
  return ptr;
}

void *realloc(void *ptr, size_t size)
{
  cgba_heap_header *old_header;
  void *new_ptr;
  size_t copy_size;

  if (!ptr)
    return malloc(size);
  if (size == 0)
    return NULL;

  old_header = ((cgba_heap_header *)ptr) - 1;
  new_ptr = malloc(size);
  if (!new_ptr)
    return NULL;

  copy_size = old_header->size < size ? old_header->size : size;
  memcpy(new_ptr, ptr, copy_size);
  return new_ptr;
}

void *memcpy(void *dst, const void *src, size_t n)
{
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  while (n--)
    *d++ = *s++;
  return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;

  if (d <= s) {
    while (n--)
      *d++ = *s++;
  } else {
    d += n;
    s += n;
    while (n--)
      *--d = *--s;
  }

  return dst;
}

void *memset(void *dst, int c, size_t n)
{
  unsigned char *d = (unsigned char *)dst;
  while (n--)
    *d++ = (unsigned char)c;
  return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;

  while (n--) {
    if (*pa != *pb)
      return (int)*pa - (int)*pb;
    pa++;
    pb++;
  }

  return 0;
}

size_t strlen(const char *s)
{
  const char *p = s;
  while (*p)
    p++;
  return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
  while (n && *a && *a == *b) {
    a++;
    b++;
    n--;
  }
  return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

char *strcpy(char *dst, const char *src)
{
  char *ret = dst;
  while ((*dst++ = *src++) != '\0')
    ;
  return ret;
}

char *strncpy(char *dst, const char *src, size_t n)
{
  char *ret = dst;
  while (n && *src) {
    *dst++ = *src++;
    n--;
  }
  while (n--) {
    *dst++ = '\0';
  }
  return ret;
}

char *strchr(const char *s, int c)
{
  while (*s) {
    if (*s == (char)c)
      return (char *)s;
    s++;
  }
  return c == 0 ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
  const char *last = NULL;
  do {
    if (*s == (char)c)
      last = s;
  } while (*s++);
  return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
  size_t needle_len = strlen(needle);

  if (!needle_len)
    return (char *)haystack;

  while (*haystack) {
    if (!strncmp(haystack, needle, needle_len))
      return (char *)haystack;
    haystack++;
  }

  return NULL;
}

static int ascii_lower(int c)
{
  if (c >= 'A' && c <= 'Z')
    return c + ('a' - 'A');
  return c;
}

int strcasecmp(const char *a, const char *b)
{
  while (*a && ascii_lower((unsigned char)*a) == ascii_lower((unsigned char)*b)) {
    a++;
    b++;
  }
  return ascii_lower((unsigned char)*a) - ascii_lower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
  while (n && *a && ascii_lower((unsigned char)*a) == ascii_lower((unsigned char)*b)) {
    a++;
    b++;
    n--;
  }
  return n ? ascii_lower((unsigned char)*a) - ascii_lower((unsigned char)*b) : 0;
}

int tolower(int c)
{
  return ascii_lower(c);
}

int toupper(int c)
{
  if (c >= 'a' && c <= 'z')
    return c - ('a' - 'A');
  return c;
}

int isalnum(int c)
{
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

int isspace(int c)
{
  return c == ' ' || c == '\f' || c == '\n' ||
         c == '\r' || c == '\t' || c == '\v';
}

char *getenv(const char *name)
{
  (void)name;
  return NULL;
}

time_t time(time_t *out)
{
  if (out)
    *out = 0;
  return 0;
}

struct tm *localtime(const time_t *timep)
{
  static struct tm tm;

  (void)timep;
  memset(&tm, 0, sizeof(tm));
  tm.tm_mday = 1;
  tm.tm_year = 100;
  return &tm;
}

void exit(int status)
{
  fxcg100_debug_puts("\n[cgba] exit ");
  fxcg100_debug_hex32((uint32_t)status);
  fxcg100_debug_puts("\n");
  fxcg100_lcd_shutdown();
  for (;;)
    ;
}

void abort(void)
{
  fxcg100_panic("abort");
  for (;;)
    ;
}

void __assert_func(const char *file, int line, const char *func,
                   const char *expr)
{
  fxcg100_debug_puts("\n[cgba] assert ");
  fxcg100_debug_puts(file ? file : "?");
  fxcg100_debug_puts(":");
  fxcg100_debug_hex32((uint32_t)line);
  fxcg100_debug_puts(" ");
  fxcg100_debug_puts(func ? func : "?");
  fxcg100_debug_puts(" ");
  fxcg100_debug_puts(expr ? expr : "?");
  fxcg100_debug_puts("\n");
  abort();
}

void __cxa_pure_virtual(void)
{
  fxcg100_panic("pure virtual");
}

void fxcg100_panic(const char *text)
{
  fxcg100_debug_puts("\n[cgba] panic: ");
  fxcg100_debug_puts(text);
  fxcg100_debug_puts("\n");
  for (;;)
    ;
}
