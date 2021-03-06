/*
    ChibiOS - Copyright (C) 2006..2015 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
   Concepts and parts of this file have been contributed by Fabio Utzig,
   chvprintf() added by Brent Roman.
 */

/**
 * @file    chprintf.c
 * @brief   Mini printf-like functionality.
 *
 * @addtogroup chprintf
 * @{
 */

#define CHPRINTF_USE_FLOAT TRUE
#include "hal.h"
#include "chprintf.h"
#include "memstreams.h"
#include "qfpio.h"

void *memcpy(void *dest, const void *src, size_t n);
void va_arg_align_if_necessary(void *ptr);

#define MAX_FILLER 11
#define FLOAT_PRECISION 9

static char *long_to_string_with_divisor(char *p,
                                         long num,
                                         unsigned radix,
                                         long divisor) {
  int i;
  char *q;
  long l, ll;

  l = num;
  if (divisor == 0) {
    ll = num;
  } else {
    ll = divisor;
  }

  q = p + MAX_FILLER;
  do {
    i = (int)(l % radix);
    i += '0';
    if (i > '9')
      i += 'A' - '0' - 10;
    *--q = i;
    l /= radix;
  } while ((ll /= radix) != 0);

  i = (int)(p + MAX_FILLER - q);
  do
    *p++ = *q++;
  while (--i);

  return p;
}

char *ch_ltoa(char *p, long num, unsigned radix) {

  return long_to_string_with_divisor(p, num, radix, 0);
}

#if CHPRINTF_USE_FLOAT
__attribute__((noinline))
static char *ftoa(char *p, float f, int width, unsigned long precision) {
  /*
  * fmt is format control word:
  * b7..b0: number of significant figures
  * b15..b8: -(minimum exponent printable in F format)
  * b23..b16: maximum exponent printable in F format-1
  * b24: output positive mantissas with ' '
  * b25: output positive mantissas with '+'
  * b26: output positive exponents with ' '
  * b27: output positive exponents with '+'
  * b28: suppress traling zeros in fraction
  * b29: fixed-point output: b7..0 give number of decimal places
  * default: 0x18060406
  * Note that if b28 is set (as it is in the default format value) the code will
  * write the trailing decimal point and zeros to the output buffer before truncating
  * the string. Thus it is essential that the output buffer is large enough to accommodate
  * these characters temporarily.
  */
  #define NUMBER_OF_SIGNIFICANT_FIGURES(x) ((x & 0xff) << 0)
  #define MAXIMUM_NEGATIVE_EXPONENT(x) ((x & 0xff) << 8)
  #define MAXIMUM_POSITIVE_EXPONENT(x) ((x & 0xff) << 16)
  #define POSITIVE_MANTISSA_SYMBOL_SPACE (1 << 24)
  #define POSITIVE_MANTISSA_SYMBOL_PLUS (1 << 25)
  #define POSITIVE_EXPONENT_SYMBOL_SPACE (1 << 26)
  #define POSITIVE_EXPONENT_SYMBOL_PLUS (1 << 27)
  #define SUPPRESS_TRAILING_ZEROES (1 << 28)
  #define FIXED_POINT_OUTPUT (1 << 29)
  uint32_t fmt = 0
      | MAXIMUM_NEGATIVE_EXPONENT(4)
      | MAXIMUM_POSITIVE_EXPONENT(6)
      | FIXED_POINT_OUTPUT
      | NUMBER_OF_SIGNIFICANT_FIGURES(precision);

  (void)width;
  // Simplify things by computing the intpart separately,
  // and letting qfp_float2str handle the float part.
  // Split along the decimal point, and hand the decimal to
  // qfp_float2str().
  /*
  int ipart = qfp_float2int(f);
  if (ipart >= 1) {
    f = qfp_fadd(f, qfp_int2float(-ipart));
  } else if (ipart <= -1) {
    f = qfp_fadd(f, qfp_int2float(-ipart));
  }
  p = ch_ltoa(p, ipart, 10);
  *p++ = '.';
*/
  return qfp_float2str(f, p, fmt);
  /*
  long l;

  if ((precision == 0) || (precision > FLOAT_PRECISION))
    precision = FLOAT_PRECISION;
  precision = pow10[precision - 1];

  l = (long)num;
  p = long_to_string_with_divisor(p, l, 10, 0);
  *p++ = '.';
  l = (long)((num - l) * precision);
  return long_to_string_with_divisor(p, l, 10, precision / 10);
  */
}
#endif

/**
 * @brief   System formatted output function.
 * @details This function implements a minimal @p vprintf()-like functionality
 *          with output on a @p BaseSequentialStream.
 *          The general parameters format is: %[-][width|*][.precision|*][l|L]p.
 *          The following parameter types (p) are supported:
 *          - <b>x</b> hexadecimal integer.
 *          - <b>X</b> hexadecimal long.
 *          - <b>o</b> octal integer.
 *          - <b>O</b> octal long.
 *          - <b>d</b> decimal signed integer.
 *          - <b>D</b> decimal signed long.
 *          - <b>u</b> decimal unsigned integer.
 *          - <b>U</b> decimal unsigned long.
 *          - <b>c</b> character.
 *          - <b>s</b> string.
 *          .
 *
 * @param[in] chp       pointer to a @p BaseSequentialStream implementing object
 * @param[in] fmt       formatting string
 * @param[in] ap        list of parameters
 * @return              The number of bytes that would have been
 *                      written to @p chp if no stream error occurs
 *
 * @api
 */
#define FLOAT_TEMP_TYPE uint32_t
int chvprintf(BaseSequentialStream *chp, const char *fmt, va_list ap) {
  char *p, *s, c, filler;
  int i, precision, width;
  int n = 0;
  bool is_long, left_align;
  long l;
#if CHPRINTF_USE_FLOAT
  float f;
  FLOAT_TEMP_TYPE ftemp;
  char tmpbuf[2*MAX_FILLER + 1];
#else
  char tmpbuf[MAX_FILLER + 1];
#endif

  while (true) {
    c = *fmt++;
    if (c == 0)
      return n;
    if (c != '%') {
      streamPut(chp, (uint8_t)c);
      n++;
      continue;
    }
    p = tmpbuf;
    s = tmpbuf;
    left_align = FALSE;
    if (*fmt == '-') {
      fmt++;
      left_align = TRUE;
    }
    filler = ' ';
    if (*fmt == '0') {
      fmt++;
      filler = '0';
    }
    width = 0;
    while (TRUE) {
      c = *fmt++;
      if (c >= '0' && c <= '9')
        c -= '0';
      else if (c == '*')
        c = va_arg(ap, int);
      else
        break;
      width = width * 10 + c;
    }
    precision = 0;
    if (c == '.') {
      while (TRUE) {
        c = *fmt++;
        if (c >= '0' && c <= '9')
          c -= '0';
        else if (c == '*')
          c = va_arg(ap, int);
        else
          break;
        precision *= 10;
        precision += c;
      }
    }
    /* Long modifier.*/
    if (c == 'l' || c == 'L') {
      is_long = TRUE;
      if (*fmt)
        c = *fmt++;
    }
    else
      is_long = (c >= 'A') && (c <= 'Z');

    /* Command decoding.*/
    switch (c) {
    case 'c':
      filler = ' ';
      *p++ = va_arg(ap, int);
      break;
    case 's':
      filler = ' ';
      if ((s = va_arg(ap, char *)) == 0)
        s = "(null)";
      if (precision == 0)
        precision = 32767;
      for (p = s; *p && (--precision >= 0); p++)
        ;
      break;
    case 'D':
    case 'd':
    case 'I':
    case 'i':
      if (is_long)
        l = va_arg(ap, long);
      else
        l = va_arg(ap, int);
      if (l < 0) {
        *p++ = '-';
        l = -l;
      }
      p = ch_ltoa(p, l, 10);
      break;
#if CHPRINTF_USE_FLOAT
    case 'f':
      // Doubles are padded with four bytes of data, but va_arg doesn't
      // seem to understand this.
      va_arg_align_if_necessary(&ap);
      ftemp = va_arg(ap, FLOAT_TEMP_TYPE);
      // We're passed 64-bit 'doubles', but on this system we ignore the
      // bottom 32-bits.  So simply discard them.
      (void)va_arg(ap, void *); 
      memcpy(&f, ((void *)&ftemp), sizeof(f));
      if (f < 0) {
        *p++ = '-';
        f = -f;
      }
      p = ftoa(p, f, width, precision);
      break;
#endif
    case 'X':
    case 'x':
      c = 16;
      goto unsigned_common;
    case 'U':
    case 'u':
      c = 10;
      goto unsigned_common;
    case 'O':
    case 'o':
      c = 8;
unsigned_common:
      if (is_long)
        l = va_arg(ap, unsigned long);
      else
        l = va_arg(ap, unsigned int);
      p = ch_ltoa(p, l, c);
      break;
    default:
      *p++ = c;
      break;
    }
    i = (int)(p - s);
    if ((width -= i) < 0)
      width = 0;
    if (left_align == FALSE)
      width = -width;
    if (width < 0) {
      if (*s == '-' && filler == '0') {
        streamPut(chp, (uint8_t)*s++);
        n++;
        i--;
      }
      do {
        streamPut(chp, (uint8_t)filler);
        n++;
      } while (++width != 0);
    }
    while (--i >= 0) {
      streamPut(chp, (uint8_t)*s++);
      n++;
    }

    while (width) {
      streamPut(chp, (uint8_t)filler);
      n++;
      width--;
    }
  }
}

/**
 * @brief   System formatted output function.
 * @details This function implements a minimal @p printf() like functionality
 *          with output on a @p BaseSequentialStream.
 *          The general parameters format is: %[-][width|*][.precision|*][l|L]p.
 *          The following parameter types (p) are supported:
 *          - <b>x</b> hexadecimal integer.
 *          - <b>X</b> hexadecimal long.
 *          - <b>o</b> octal integer.
 *          - <b>O</b> octal long.
 *          - <b>d</b> decimal signed integer.
 *          - <b>D</b> decimal signed long.
 *          - <b>u</b> decimal unsigned integer.
 *          - <b>U</b> decimal unsigned long.
 *          - <b>c</b> character.
 *          - <b>s</b> string.
 *          .
 *
 * @param[in] chp       pointer to a @p BaseSequentialStream implementing object
 * @param[in] fmt       formatting string
 *
 * @api
 */
int chprintf(BaseSequentialStream *chp, const char *fmt, ...) {
  va_list ap;
  int formatted_bytes;

  va_start(ap, fmt);
  formatted_bytes = chvprintf(chp, fmt, ap);
  va_end(ap);

  return formatted_bytes;
}

/**
 * @brief   System formatted output function.
 * @details This function implements a minimal @p vprintf()-like functionality
 *          with output on a @p BaseSequentialStream.
 *          The general parameters format is: %[-][width|*][.precision|*][l|L]p.
 *          The following parameter types (p) are supported:
 *          - <b>x</b> hexadecimal integer.
 *          - <b>X</b> hexadecimal long.
 *          - <b>o</b> octal integer.
 *          - <b>O</b> octal long.
 *          - <b>d</b> decimal signed integer.
 *          - <b>D</b> decimal signed long.
 *          - <b>u</b> decimal unsigned integer.
 *          - <b>U</b> decimal unsigned long.
 *          - <b>c</b> character.
 *          - <b>s</b> string.
 *          .
 * @post    @p str is NUL-terminated, unless @p size is 0.
 *
 * @param[in] str       pointer to a buffer
 * @param[in] size      maximum size of the buffer
 * @param[in] fmt       formatting string
 * @return              The number of characters (excluding the
 *                      terminating NUL byte) that would have been
 *                      stored in @p str if there was room.
 *
 * @api
 */
int chsnprintf(char *str, size_t size, const char *fmt, ...) {
  va_list ap;
  MemoryStream ms;
  BaseSequentialStream *chp;
  size_t size_wo_nul;
  int retval;

  if (size > 0)
    size_wo_nul = size - 1;
  else
    size_wo_nul = 0;

  /* Memory stream object to be used as a string writer, reserving one
     byte for the final zero.*/
  msObjectInit(&ms, (uint8_t *)str, size_wo_nul, 0);

  /* Performing the print operation using the common code.*/
  chp = (BaseSequentialStream *)(void *)&ms;
  va_start(ap, fmt);
  retval = chvprintf(chp, fmt, ap);
  va_end(ap);

  /* Terminate with a zero, unless size==0.*/
  if (ms.eos < size)
      str[ms.eos] = 0;

  /* Return number of bytes that would have been written.*/
  return retval;
}

/** @} */
