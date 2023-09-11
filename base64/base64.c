/**
 * @ref mbedtls base64.c
 */

#include "base64.h"

#include <stdint.h>

#define BASE64_BYTE_0(x) ((uint8_t) ((x)         & 0xff))
#define BASE64_BYTE_1(x) ((uint8_t) (((x) >>  8) & 0xff))
#define BASE64_BYTE_2(x) ((uint8_t) (((x) >> 16) & 0xff))

unsigned char uchar_mask_of_range(unsigned char low,
								  unsigned char high,
								  unsigned char c)
{
    /* low_mask is: 0 if low <= c, 0x...ff if low > c */
    unsigned low_mask = ((unsigned) c - low) >> 8;
    /* high_mask is: 0 if c <= high, 0x...ff if c > high */
    unsigned high_mask = ((unsigned) high - c) >> 8;
    return ~(low_mask | high_mask) & 0xff;
}

unsigned char base64_enc_char(unsigned char value)
{
    unsigned char digit = 0;
    /* For each range of values, if value is in that range, mask digit with
     * the corresponding value. Since value can only be in a single range,
     * only at most one masking will change digit. */
    digit |= uchar_mask_of_range(0, 25, value) & ('A' + value);
    digit |= uchar_mask_of_range(26, 51, value) & ('a' + value - 26);
    digit |= uchar_mask_of_range(52, 61, value) & ('0' + value - 52);
    digit |= uchar_mask_of_range(62, 62, value) & '+';
    digit |= uchar_mask_of_range(63, 63, value) & '/';
    return digit;
}

signed char base64_dec_value(unsigned char c)
{
    unsigned char val = 0;
    /* For each range of digits, if c is in that range, mask val with
     * the corresponding value. Since c can only be in a single range,
     * only at most one masking will change val. Set val to one plus
     * the desired value so that it stays 0 if c is in none of the ranges. */
    val |= uchar_mask_of_range('A', 'Z', c) & (c - 'A' +  0 + 1);
    val |= uchar_mask_of_range('a', 'z', c) & (c - 'a' + 26 + 1);
    val |= uchar_mask_of_range('0', '9', c) & (c - '0' + 52 + 1);
    val |= uchar_mask_of_range('+', '+', c) & (c - '+' + 62 + 1);
    val |= uchar_mask_of_range('/', '/', c) & (c - '/' + 63 + 1);
    /* At this point, val is 0 if c is an invalid digit and v+1 if c is
     * a digit with the value v. */
    return val - 1;
}

/*
 * Encode a buffer into base64 format
 */
int base64_encode(unsigned char *dst, size_t dlen, size_t *olen,
				  const unsigned char *src, size_t slen)
{
    size_t i, n;
    int C1, C2, C3;
    unsigned char *p;

    if (slen == 0) {
        *olen = 0;
        return 0;
    }

    n = slen / 3 + (slen % 3 != 0);

    if (n > (SIZE_MAX - 1) / 4) {
        *olen = SIZE_MAX;
        return BASE64_BUFFER_TOO_SMALL;
    }

    n *= 4;

    if ((dlen < n + 1) || (NULL == dst)) {
        *olen = n + 1;
        return BASE64_BUFFER_TOO_SMALL;
    }

    n = (slen / 3) * 3;

    for (i = 0, p = dst; i < n; i += 3) {
        C1 = *src++;
        C2 = *src++;
        C3 = *src++;

        *p++ = base64_enc_char((C1 >> 2) & 0x3F);
        *p++ = base64_enc_char((((C1 &  3) << 4) + (C2 >> 4))
                                          & 0x3F);
        *p++ = base64_enc_char((((C2 & 15) << 2) + (C3 >> 6))
                                          & 0x3F);
        *p++ = base64_enc_char(C3 & 0x3F);
    }

    if (i < slen) {
        C1 = *src++;
        C2 = ((i + 1) < slen) ? *src++ : 0;

        *p++ = base64_enc_char((C1 >> 2) & 0x3F);
        *p++ = base64_enc_char((((C1 & 3) << 4) + (C2 >> 4))
                                          & 0x3F);

        if ((i + 1) < slen) {
            *p++ = base64_enc_char(((C2 & 15) << 2) & 0x3F);
        } else {
            *p++ = '=';
        }

        *p++ = '=';
    }

    *olen = p - dst;
    *p = 0;

    return 0;
}

/*
 * Decode a base64-formatted buffer
 */
int base64_decode(unsigned char *dst, size_t dlen, size_t *olen,
				  const unsigned char *src, size_t slen)
{
    size_t i; /* index in source */
    size_t n; /* number of digits or trailing = in source */
    uint32_t x; /* value accumulator */
    uint32_t accumulated_digits = 0;
    uint32_t equals = 0;
    int spaces_present = 0;
    unsigned char *p;

    /* First pass: check for validity and get output length */
    for (i = n = 0; i < slen; i++) {
        /* Skip spaces before checking for EOL */
        spaces_present = 0;
        while (i < slen && src[i] == ' ') {
            ++i;
            spaces_present = 1;
        }

        /* Spaces at end of buffer are OK */
        if (i == slen) {
            break;
        }

        if ((slen - i) >= 2 &&
            src[i] == '\r' && src[i + 1] == '\n') {
            continue;
        }

        if (src[i] == '\n') {
            continue;
        }

        /* Space inside a line is an error */
        if (spaces_present) {
            return BASE64_INVALID_CHARACTER;
        }

        if (src[i] > 127) {
            return BASE64_INVALID_CHARACTER;
        }

        if (src[i] == '=') {
            if (++equals > 2) {
                return BASE64_INVALID_CHARACTER;
            }
        } else {
            if (equals != 0) {
                return BASE64_INVALID_CHARACTER;
            }
            if (base64_dec_value(src[i]) < 0) {
                return BASE64_INVALID_CHARACTER;
            }
        }
        n++;
    }

    if (n == 0) {
        *olen = 0;
        return 0;
    }

    /* The following expression is to calculate the following formula without
     * risk of integer overflow in n:
     *     n = ( ( n * 6 ) + 7 ) >> 3;
     */
    n = (6 * (n >> 3)) + ((6 * (n & 0x7) + 7) >> 3);
    n -= equals;

    if (dst == NULL || dlen < n) {
        *olen = n;
        return BASE64_BUFFER_TOO_SMALL;
    }

    equals = 0;
    for (x = 0, p = dst; i > 0; i--, src++) {
        if (*src == '\r' || *src == '\n' || *src == ' ') {
            continue;
        }

        x = x << 6;
        if (*src == '=') {
            ++equals;
        } else {
            x |= base64_dec_value(*src);
        }

        if (++accumulated_digits == 4) {
            accumulated_digits = 0;
            *p++ = BASE64_BYTE_2(x);
            if (equals <= 1) {
                *p++ = BASE64_BYTE_1(x);
            }
            if (equals <= 0) {
                *p++ = BASE64_BYTE_0(x);
            }
        }
    }

    *olen = p - dst;

    return 0;
}
