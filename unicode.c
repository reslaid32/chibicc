#include "chibicc.h"

// Encode a given character in UTF-8.
int
encode_utf8(char* buf, uint32_t c)
{
  if (c <= 0x7F)
  {
    buf[0] = c;
    return 1;
  }

  if (c <= 0x7FF)
  {
    buf[0] = 0b11000000 | (c >> 6);
    buf[1] = 0b10000000 | (c & 0b00111111);
    return 2;
  }

  if (c <= 0xFFFF)
  {
    buf[0] = 0b11100000 | (c >> 12);
    buf[1] = 0b10000000 | ((c >> 6) & 0b00111111);
    buf[2] = 0b10000000 | (c & 0b00111111);
    return 3;
  }

  buf[0] = 0b11110000 | (c >> 18);
  buf[1] = 0b10000000 | ((c >> 12) & 0b00111111);
  buf[2] = 0b10000000 | ((c >> 6) & 0b00111111);
  buf[3] = 0b10000000 | (c & 0b00111111);
  return 4;
}

// Read a UTF-8-encoded Unicode code point from a source file.
// We assume that source files are always in UTF-8.
//
// UTF-8 is a variable-width encoding in which one code point is
// encoded in one to four bytes. One byte UTF-8 code points are
// identical to ASCII. Non-ASCII characters are encoded using more
// than one byte.
uint32_t
decode_utf8(char** new_pos, char* p)
{
  if ((unsigned char) *p < 128)
  {
    *new_pos = p + 1;
    return *p;
  }

  char* start = p;
  int len;
  uint32_t c;

  if ((unsigned char) *p >= 0b11110000)
  {
    len = 4;
    c = *p & 0b111;
  }
  else if ((unsigned char) *p >= 0b11100000)
  {
    len = 3;
    c = *p & 0b1111;
  }
  else if ((unsigned char) *p >= 0b11000000)
  {
    len = 2;
    c = *p & 0b11111;
  }
  else
  {
    error_at(start, "invalid UTF-8 sequence");
  }

  for (int i = 1; i < len; i++)
  {
    if ((unsigned char) p[i] >> 6 != 0b10)
      error_at(start, "invalid UTF-8 sequence");
    c = (c << 6) | (p[i] & 0b111111);
  }

  *new_pos = p + len;
  return c;
}

static bool
in_range(uint32_t* range, uint32_t c)
{
  for (int i = 0; range[i] != -1; i += 2)
    if (range[i] <= c && c <= range[i + 1])
      return true;
  return false;
}

// [https://www.sigbus.info/n1570#D] C11 allows not only ASCII but
// some multibyte characters in certan Unicode ranges to be used in an
// identifier.
//
// This function returns true if a given character is acceptable as
// the first character of an identifier.
//
// For example, ¾ (U+00BE) is a valid identifier because characters in
// 0x00BE-0x00C0 are allowed, while neither ⟘ (U+27D8) nor '　'
// (U+3000, full-width space) are allowed because they are out of range.
bool
is_ident1(uint32_t c)
{
  static uint32_t range[] = {
    '_',     '_',     'a',     'z',     'A',     'Z',     '$',     '$',
    0x00A8,  0x00A8,  0x00AA,  0x00AA,  0x00AD,  0x00AD,  0x00AF,  0x00AF,
    0x00B2,  0x00B5,  0x00B7,  0x00BA,  0x00BC,  0x00BE,  0x00C0,  0x00D6,
    0x00D8,  0x00F6,  0x00F8,  0x00FF,  0x0100,  0x02FF,  0x0370,  0x167F,
    0x1681,  0x180D,  0x180F,  0x1DBF,  0x1E00,  0x1FFF,  0x200B,  0x200D,
    0x202A,  0x202E,  0x203F,  0x2040,  0x2054,  0x2054,  0x2060,  0x206F,
    0x2070,  0x20CF,  0x2100,  0x218F,  0x2460,  0x24FF,  0x2776,  0x2793,
    0x2C00,  0x2DFF,  0x2E80,  0x2FFF,  0x3004,  0x3007,  0x3021,  0x302F,
    0x3031,  0x303F,  0x3040,  0xD7FF,  0xF900,  0xFD3D,  0xFD40,  0xFDCF,
    0xFDF0,  0xFE1F,  0xFE30,  0xFE44,  0xFE47,  0xFFFD,  0x10000, 0x1FFFD,
    0x20000, 0x2FFFD, 0x30000, 0x3FFFD, 0x40000, 0x4FFFD, 0x50000, 0x5FFFD,
    0x60000, 0x6FFFD, 0x70000, 0x7FFFD, 0x80000, 0x8FFFD, 0x90000, 0x9FFFD,
    0xA0000, 0xAFFFD, 0xB0000, 0xBFFFD, 0xC0000, 0xCFFFD, 0xD0000, 0xDFFFD,
    0xE0000, 0xEFFFD, -1,
  };

  return in_range(range, c);
}

// Returns true if a given character is acceptable as a non-first
// character of an identifier.
bool
is_ident2(uint32_t c)
{
  static uint32_t range[] = {
    '0',    '9',    '$',    '$',    0x0300, 0x036F, 0x1DC0,
    0x1DFF, 0x20D0, 0x20FF, 0xFE20, 0xFE2F, -1,
  };

  return is_ident1(c) || in_range(range, c);
}

// Returns the number of columns needed to display a given
// character in a fixed-width font.
//
// Based on https://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
static int
char_width(uint32_t c)
{
  static uint32_t range1[] = {
    0x0000,  0x001F,  0x007f,  0x00a0,  0x0300,  0x036F,  0x0483,  0x0486,
    0x0488,  0x0489,  0x0591,  0x05BD,  0x05BF,  0x05BF,  0x05C1,  0x05C2,
    0x05C4,  0x05C5,  0x05C7,  0x05C7,  0x0600,  0x0603,  0x0610,  0x0615,
    0x064B,  0x065E,  0x0670,  0x0670,  0x06D6,  0x06E4,  0x06E7,  0x06E8,
    0x06EA,  0x06ED,  0x070F,  0x070F,  0x0711,  0x0711,  0x0730,  0x074A,
    0x07A6,  0x07B0,  0x07EB,  0x07F3,  0x0901,  0x0902,  0x093C,  0x093C,
    0x0941,  0x0948,  0x094D,  0x094D,  0x0951,  0x0954,  0x0962,  0x0963,
    0x0981,  0x0981,  0x09BC,  0x09BC,  0x09C1,  0x09C4,  0x09CD,  0x09CD,
    0x09E2,  0x09E3,  0x0A01,  0x0A02,  0x0A3C,  0x0A3C,  0x0A41,  0x0A42,
    0x0A47,  0x0A48,  0x0A4B,  0x0A4D,  0x0A70,  0x0A71,  0x0A81,  0x0A82,
    0x0ABC,  0x0ABC,  0x0AC1,  0x0AC5,  0x0AC7,  0x0AC8,  0x0ACD,  0x0ACD,
    0x0AE2,  0x0AE3,  0x0B01,  0x0B01,  0x0B3C,  0x0B3C,  0x0B3F,  0x0B3F,
    0x0B41,  0x0B43,  0x0B4D,  0x0B4D,  0x0B56,  0x0B56,  0x0B82,  0x0B82,
    0x0BC0,  0x0BC0,  0x0BCD,  0x0BCD,  0x0C3E,  0x0C40,  0x0C46,  0x0C48,
    0x0C4A,  0x0C4D,  0x0C55,  0x0C56,  0x0CBC,  0x0CBC,  0x0CBF,  0x0CBF,
    0x0CC6,  0x0CC6,  0x0CCC,  0x0CCD,  0x0CE2,  0x0CE3,  0x0D41,  0x0D43,
    0x0D4D,  0x0D4D,  0x0DCA,  0x0DCA,  0x0DD2,  0x0DD4,  0x0DD6,  0x0DD6,
    0x0E31,  0x0E31,  0x0E34,  0x0E3A,  0x0E47,  0x0E4E,  0x0EB1,  0x0EB1,
    0x0EB4,  0x0EB9,  0x0EBB,  0x0EBC,  0x0EC8,  0x0ECD,  0x0F18,  0x0F19,
    0x0F35,  0x0F35,  0x0F37,  0x0F37,  0x0F39,  0x0F39,  0x0F71,  0x0F7E,
    0x0F80,  0x0F84,  0x0F86,  0x0F87,  0x0F90,  0x0F97,  0x0F99,  0x0FBC,
    0x0FC6,  0x0FC6,  0x102D,  0x1030,  0x1032,  0x1032,  0x1036,  0x1037,
    0x1039,  0x1039,  0x1058,  0x1059,  0x1160,  0x11FF,  0x135F,  0x135F,
    0x1712,  0x1714,  0x1732,  0x1734,  0x1752,  0x1753,  0x1772,  0x1773,
    0x17B4,  0x17B5,  0x17B7,  0x17BD,  0x17C6,  0x17C6,  0x17C9,  0x17D3,
    0x17DD,  0x17DD,  0x180B,  0x180D,  0x18A9,  0x18A9,  0x1920,  0x1922,
    0x1927,  0x1928,  0x1932,  0x1932,  0x1939,  0x193B,  0x1A17,  0x1A18,
    0x1B00,  0x1B03,  0x1B34,  0x1B34,  0x1B36,  0x1B3A,  0x1B3C,  0x1B3C,
    0x1B42,  0x1B42,  0x1B6B,  0x1B73,  0x1DC0,  0x1DCA,  0x1DFE,  0x1DFF,
    0x200B,  0x200F,  0x202A,  0x202E,  0x2060,  0x2063,  0x206A,  0x206F,
    0x20D0,  0x20EF,  0x302A,  0x302F,  0x3099,  0x309A,  0xA806,  0xA806,
    0xA80B,  0xA80B,  0xA825,  0xA826,  0xFB1E,  0xFB1E,  0xFE00,  0xFE0F,
    0xFE20,  0xFE23,  0xFEFF,  0xFEFF,  0xFFF9,  0xFFFB,  0x10A01, 0x10A03,
    0x10A05, 0x10A06, 0x10A0C, 0x10A0F, 0x10A38, 0x10A3A, 0x10A3F, 0x10A3F,
    0x1D167, 0x1D169, 0x1D173, 0x1D182, 0x1D185, 0x1D18B, 0x1D1AA, 0x1D1AD,
    0x1D242, 0x1D244, 0xE0001, 0xE0001, 0xE0020, 0xE007F, 0xE0100, 0xE01EF,
    -1,
  };

  if (in_range(range1, c))
    return 0;

  static uint32_t range2[] = {
    0x1100,  0x115F,  0x2329,  0x2329,  0x232A, 0x232A, 0x2E80,  0x303E,
    0x3040,  0xA4CF,  0xAC00,  0xD7A3,  0xF900, 0xFAFF, 0xFE10,  0xFE19,
    0xFE30,  0xFE6F,  0xFF00,  0xFF60,  0xFFE0, 0xFFE6, 0x1F000, 0x1F644,
    0x20000, 0x2FFFD, 0x30000, 0x3FFFD, -1,
  };

  if (in_range(range2, c))
    return 2;
  return 1;
}

// Returns the number of columns needed to display a given
// string in a fixed-width font.
int
display_width(char* p, int len)
{
  char* start = p;
  int w = 0;
  while (p - start < len)
  {
    uint32_t c = decode_utf8(&p, p);
    w += char_width(c);
  }
  return w;
}
