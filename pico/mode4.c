/*
 * SMS renderer
 * (C) notaz, 2009-2010
 * (C) kub, 2021
 *
 * currently supports VDP mode 4 (SMS and GG) and mode 2
 *
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
/*
 * TODO:
 * - other TMS9918 modes?
 */
#include "pico_int.h"
#include <platform/common/upscale.h>

static void (*FinalizeLineSMS)(int line);
static int skip_next_line;
static int screen_offset, line_offset;


/* Mode 4 */
/*========*/

static void TileBGM4(u16 sx, int pal)
{
  u32 *pd = (u32 *)(Pico.est.HighCol + sx);
  pd[0] = pd[1] = pal ? 0x10101010 : 0;
}

// 8 pixels are arranged to have 1 bit in each byte of a 32 bit word. To pull
// the 4 bitplanes together multiply with each bit distance (multiples of 1<<7)
#define PLANAR_PIXELL(x,p) \
  t = (pack>>(7-p)) & 0x01010101; \
  t = (t*0x10204080) >> 28; \
  pd[x] = pal|t;

static void TileNormLowM4(u16 sx, unsigned int pack, int pal)
{
  u8 *pd = Pico.est.HighCol + sx;
  u32 t;

  PLANAR_PIXELL(0, 0)
  PLANAR_PIXELL(1, 1)
  PLANAR_PIXELL(2, 2)
  PLANAR_PIXELL(3, 3)
  PLANAR_PIXELL(4, 4)
  PLANAR_PIXELL(5, 5)
  PLANAR_PIXELL(6, 6)
  PLANAR_PIXELL(7, 7)
}

static void TileFlipLowM4(u16 sx, unsigned int pack, int pal)
{
  u8 *pd = Pico.est.HighCol + sx;
  u32 t;

  PLANAR_PIXELL(0, 7)
  PLANAR_PIXELL(1, 6)
  PLANAR_PIXELL(2, 5)
  PLANAR_PIXELL(3, 4)
  PLANAR_PIXELL(4, 3)
  PLANAR_PIXELL(5, 2)
  PLANAR_PIXELL(6, 1)
  PLANAR_PIXELL(7, 0)
}

#define PLANAR_PIXEL(x,p) \
  t = (pack>>(7-p)) & 0x01010101; \
  if (t) { \
    t = (t*0x10204080) >> 28; \
    pd[x] = pal|t; \
  }

static void TileNormM4(u16 sx, unsigned int pack, int pal)
{
  u8 *pd = Pico.est.HighCol + sx;
  u32 t;

  PLANAR_PIXEL(0, 0)
  PLANAR_PIXEL(1, 1)
  PLANAR_PIXEL(2, 2)
  PLANAR_PIXEL(3, 3)
  PLANAR_PIXEL(4, 4)
  PLANAR_PIXEL(5, 5)
  PLANAR_PIXEL(6, 6)
  PLANAR_PIXEL(7, 7)
}

static void TileFlipM4(u16 sx, unsigned int pack, int pal)
{
  u8 *pd = Pico.est.HighCol + sx;
  u32 t;

  PLANAR_PIXEL(0, 7)
  PLANAR_PIXEL(1, 6)
  PLANAR_PIXEL(2, 5)
  PLANAR_PIXEL(3, 4)
  PLANAR_PIXEL(4, 3)
  PLANAR_PIXEL(5, 2)
  PLANAR_PIXEL(6, 1)
  PLANAR_PIXEL(7, 0)
}

static void TileDoubleM4(int sx, unsigned int pack, int pal)
{
  u8 *pd = Pico.est.HighCol + sx;
  u32 t;

  PLANAR_PIXEL(0, 0)
  PLANAR_PIXEL(1, 0)
  PLANAR_PIXEL(2, 1)
  PLANAR_PIXEL(3, 1)
  PLANAR_PIXEL(4, 2)
  PLANAR_PIXEL(5, 2)
  PLANAR_PIXEL(6, 3)
  PLANAR_PIXEL(7, 3)
  PLANAR_PIXEL(8, 4)
  PLANAR_PIXEL(9, 4)
  PLANAR_PIXEL(10, 5)
  PLANAR_PIXEL(11, 5)
  PLANAR_PIXEL(12, 6)
  PLANAR_PIXEL(13, 6)
  PLANAR_PIXEL(14, 7)
  PLANAR_PIXEL(15, 7)
}

static void DrawSpritesM4(int scanline)
{
  struct PicoVideo *pv = &Pico.video;
  unsigned int sprites_addr[8];
  unsigned int sprites_x[8];
  unsigned int pack;
  u8 *sat;
  int xoff = 8; // relative to HighCol, which is (screen - 8)
  int sprite_base, addr_mask;
  int zoomed = pv->reg[1] & 0x1; // zoomed sprites, e.g. Earthworm Jim
  int i, s, h;

  if (pv->reg[0] & 8)
    xoff = 0;
  xoff += line_offset;

  sat = (u8 *)PicoMem.vram + ((pv->reg[5] & 0x7e) << 7);
  if (pv->reg[1] & 2) {
    addr_mask = 0xfe; h = 16;
  } else {
    addr_mask = 0xff; h = 8;
  }
  if (zoomed) h *= 2;
  sprite_base = (pv->reg[6] & 4) << (13-2-1);

  for (i = s = 0; i < 64; i++)
  {
    int y;
    y = (sat[MEM_LE2(i)] + 1) & 0xff;
    if (y == 0xd1 && !((pv->reg[0] & 6) == 6 && (pv->reg[1] & 0x18)))
      break;
    if (y + h <= scanline || scanline < y)
      continue; // not on this line
    if (s >= 8) {
      pv->status |= SR_SOVR;
      break;
    }

    sprites_x[s] = xoff + sat[MEM_LE2(0x80 + i*2)];
    sprites_addr[s] = sprite_base + ((sat[MEM_LE2(0x80 + i*2 + 1)] & addr_mask) << (5-1)) +
      ((scanline - y) >> zoomed << (2-1));
    s++;
  }

  // really half-assed but better than nothing
  if (s > 1)
    pv->status |= SR_C;

  // now draw all sprites backwards
  for (--s; s >= 0; s--) {
    pack = CPU_LE2(*(u32 *)(PicoMem.vram + sprites_addr[s]));
    if (zoomed) TileDoubleM4(sprites_x[s], pack, 0x10);
    else        TileNormM4(sprites_x[s], pack, 0x10);
  }
}

// cells_dx, tilex_ty merged to reduce register pressure
static void DrawStripLowM4(const u16 *nametab, int cells_dx, int tilex_ty)
{
  int oldcode = -1;
  int addr = 0, pal = 0;

  // Draw tiles across screen:
  for (; cells_dx > 0; cells_dx += 8, tilex_ty++, cells_dx -= 0x10000)
  {
    unsigned int pack;
    unsigned code;

    code = nametab[tilex_ty& 0x1f];
    if (code & 0x1000) // priority high?
      continue;

    if (code != oldcode) {
      oldcode = code;
      // Get tile address/2:
      addr = (code & 0x1ff) << 4;
      addr += tilex_ty>> 16;
      if (code & 0x0400)
        addr ^= 0xe; // Y-flip

      pal = (code>>7) & 0x10;
    }

    pack = CPU_LE2(*(u32 *)(PicoMem.vram + addr)); /* Get 4 bitplanes / 8 pixels */
    if (pack == 0)          TileBGM4(cells_dx, pal);
    else if (code & 0x0200) TileFlipLowM4(cells_dx, pack, pal);
    else                    TileNormLowM4(cells_dx, pack, pal);
  }
}

static void DrawStripHighM4(const u16 *nametab, int cells_dx, int tilex_ty)
{
  int oldcode = -1, blank = -1; // The tile we know is blank
  int addr = 0, pal = 0;

  // Draw tiles across screen:
  for (; cells_dx > 0; cells_dx += 8, tilex_ty++, cells_dx -= 0x10000)
  {
    unsigned int pack;
    unsigned code;

    code = nametab[tilex_ty& 0x1f];
    if (code == blank)
      continue;
    if (!(code & 0x1000)) // priority low?
      continue;

    if (code != oldcode) {
      oldcode = code;
      // Get tile address/2:
      addr = (code & 0x1ff) << 4;
      addr += tilex_ty>> 16;
      if (code & 0x0400)
        addr ^= 0xe; // Y-flip

      pal = (code>>7) & 0x10;
    }

    pack = CPU_LE2(*(u32 *)(PicoMem.vram + addr)); /* Get 4 bitplanes / 8 pixels */
    if (pack == 0) {
      blank = code;
      continue;
    }
    if (code & 0x0200) TileFlipM4(cells_dx, pack, pal);
    else               TileNormM4(cells_dx, pack, pal);
  }
}

static void DrawDisplayM4(int scanline)
{
  struct PicoVideo *pv = &Pico.video;
  u16 *nametab, *nametab2;
  int line, tilex, dx, ty, cells;
  int cellskip = 0; // XXX
  int maxcells = 32;

  // Find the line in the name table
  line = pv->reg[9] + scanline; // vscroll + scanline

  // Find name table:
  nametab = PicoMem.vram;
  if ((pv->reg[0] & 6) == 6 && (pv->reg[1] & 0x18)) {
    // 224/240 line mode
    line &= 0xff;
    nametab += ((pv->reg[2] & 0x0c) << (10-1)) + (0x700 >> 1);
  } else {
    while (line >= 224) line -= 224;
    nametab += (pv->reg[2] & 0x0e) << (10-1);
    // old SMS only, masks line:7 with reg[2]:0 for address calculation
    //if ((pv->reg[2] & 0x01) == 0) line &= 0x7f;
  }
  nametab2 = nametab + ((scanline>>3) << (6-1));
  nametab  = nametab + ((line>>3)     << (6-1));

  dx = pv->reg[8]; // hscroll
  if (scanline < 16 && (pv->reg[0] & 0x40))
    dx = 0; // hscroll disabled for top 2 rows (e.g. Fantasy Zone II)

  tilex = ((-dx >> 3) + cellskip) & 0x1f;
  ty = (line & 7) << 1; // Y-Offset into tile
  cells = maxcells - cellskip;

  dx = ((dx - 1) & 7) + 1;
  if (dx != 8)
    cells++; // have hscroll, need to draw 1 cell more
  dx += cellskip << 3;
  dx += line_offset;

  // low priority tiles
  if (!(pv->debug_p & PVD_KILL_B)) {
    if (pv->reg[0] & 0x80) {
      // vscroll disabled for rightmost 8 columns (e.g. Gauntlet)
      int dx2 = dx + (cells-8)*8, tilex2 = tilex + (cells-8), ty2 = scanline & 7;
      DrawStripLowM4(nametab,  dx | ((cells-8) << 16), tilex  | (ty  << 16));
      DrawStripLowM4(nametab2, dx2 |       (8  << 16), tilex2 | (ty2 << 17));
    } else
      DrawStripLowM4(nametab , dx | ( cells    << 16), tilex  | (ty  << 16));
  }

  // sprites
  if (!(pv->debug_p & PVD_KILL_S_LO))
    DrawSpritesM4(scanline);

  // high priority tiles (use virtual layer switch just for fun)
  if (!(pv->debug_p & PVD_KILL_A)) {
    if (pv->reg[0] & 0x80) {
      int dx2 = dx + (cells-8)*8, tilex2 = tilex + (cells-8), ty2 = scanline & 7;
      DrawStripHighM4(nametab,  dx | ((cells-8) << 16), tilex  | (ty  << 16));
      DrawStripHighM4(nametab2, dx2 |       (8  << 16), tilex2 | (ty2 << 17));
    } else
      DrawStripHighM4(nametab , dx | ( cells    << 16), tilex  | (ty  << 16));
  }

  if (pv->reg[0] & 0x20) {
    // first column masked with background, caculate offset to start of line
    dx = (dx&~0x1f) / 4;
    ty = 0xe0e0e0e0; // really (pv->reg[7]&0x3f) * 0x01010101, but the looks...
    ((u32 *)Pico.est.HighCol)[dx+2] = ((u32 *)Pico.est.HighCol)[dx+3] = ty;
  }
}


/* Mode 2 */
/*========*/

/* Background */

#define TMS_PIXELBG(x,p) \
  t = (pack>>(7-p)) & 0x01; \
  t = (pal >> (t << 2)) & 0x0f; \
  pd[x] = t;

static void TileNormBgM2(u16 sx, unsigned int pack, int pal)
{
  u8 *pd = Pico.est.HighCol + sx;
  unsigned int t;

  TMS_PIXELBG(0, 0)
  TMS_PIXELBG(1, 1)
  TMS_PIXELBG(2, 2)
  TMS_PIXELBG(3, 3)
  TMS_PIXELBG(4, 4)
  TMS_PIXELBG(5, 5)
  TMS_PIXELBG(6, 6)
  TMS_PIXELBG(7, 7)
}

/* Sprites */

#define TMS_PIXELSP(x,p) \
  t = (pack>>(7-p)) & 0x01; \
  if (t) \
    pd[x] = pal;

static void TileNormSprM2(u16 sx, unsigned int pack, int pal)
{
  u8 *pd = Pico.est.HighCol + sx;
  unsigned int t;

  TMS_PIXELSP(0, 0)
  TMS_PIXELSP(1, 1)
  TMS_PIXELSP(2, 2)
  TMS_PIXELSP(3, 3)
  TMS_PIXELSP(4, 4)
  TMS_PIXELSP(5, 5)
  TMS_PIXELSP(6, 6)
  TMS_PIXELSP(7, 7)
}

static void TileDoubleSprM2(u16 sx, unsigned int pack, int pal)
{
  u8 *pd = Pico.est.HighCol + sx;
  unsigned int t;

  TMS_PIXELSP(0, 0)
  TMS_PIXELSP(1, 0)
  TMS_PIXELSP(2, 1)
  TMS_PIXELSP(3, 1)
  TMS_PIXELSP(4, 2)
  TMS_PIXELSP(5, 2)
  TMS_PIXELSP(6, 3)
  TMS_PIXELSP(7, 3)
  TMS_PIXELSP(8, 4)
  TMS_PIXELSP(9, 4)
  TMS_PIXELSP(10, 5)
  TMS_PIXELSP(11, 5)
  TMS_PIXELSP(12, 6)
  TMS_PIXELSP(13, 6)
  TMS_PIXELSP(14, 7)
  TMS_PIXELSP(15, 7)
}

/* Draw sprites into a scanline, max 4 */
static void DrawSpritesM2(int scanline)
{
  struct PicoVideo *pv = &Pico.video;
  unsigned int sprites_addr[4];
  unsigned int sprites_x[4];
  unsigned int pack;
  u8 *sat;
  int xoff = 8; // relative to HighCol, which is (screen - 8)
  int sprite_base, addr_mask;
  int zoomed = pv->reg[1] & 0x1; // zoomed sprites
  int i, s, h;

  xoff += line_offset;

  sat = (u8 *)PicoMem.vramb + ((pv->reg[5] & 0x7e) << 7);
  if (pv->reg[1] & 2) {
    addr_mask = 0xfc; h = 16;
  } else {
    addr_mask = 0xff; h = 8;
  }
  if (zoomed) h *= 2;

  sprite_base = (pv->reg[6] & 0x7) << 11;

  /* find sprites on this scanline */
  for (i = s = 0; i < 32; i++)
  {
    int y;
    y = (sat[MEM_LE2(4*i)] + 1) & 0xff;
    if (y == 0xd1)
      break;
    if (y > 0xe0)
      y -= 256;
    if (y + h <= scanline || scanline < y)
      continue; // not on this line
    if (s >= 4) {
      pv->status |= SR_SOVR | i;
      break;
    }

    sprites_x[s] = 4*i;
    sprites_addr[s] = sprite_base + ((sat[MEM_LE2(4*i + 2)] & addr_mask) << 3) +
      ((scanline - y) >> zoomed);
    s++;
  }

  // really half-assed but better than nothing
  if (s > 1)
    pv->status |= SR_C;

  // now draw all sprites backwards
  for (--s; s >= 0; s--) {
    int x, w = (zoomed ? 16: 8);
    i = sprites_x[s];
    x = sat[MEM_LE2(i+1)] + xoff;
    if (sat[MEM_LE2(i+3)] & 0x80)
      x -= 32;
    if (x > 0) {
      pack = PicoMem.vramb[MEM_LE2(sprites_addr[s])];
      if (zoomed) TileDoubleSprM2(x, pack, sat[MEM_LE2(i+3)] & 0xf);
      else        TileNormSprM2(x, pack, sat[MEM_LE2(i+3)] & 0xf);
    }
    if((pv->reg[1] & 0x2) && (x+=w) > 0) {
      pack = PicoMem.vramb[MEM_LE2(sprites_addr[s]+0x10)];
      if (zoomed) TileDoubleSprM2(x, pack, sat[MEM_LE2(i+3)] & 0xf);
      else        TileNormSprM2(x, pack, sat[MEM_LE2(i+3)] & 0xf);
    }
  }
}

/* Draw the background into a scanline; cells, dx, tilex, ty merged to reduce registers */
static void DrawStripM2(const u8 *nametab, const u8 *coltab, const u8 *pattab, int cells_dx, int tilex_ty)
{
  // Draw tiles across screen:
  for (; cells_dx > 0; cells_dx += 8, tilex_ty++, cells_dx -= 0x10000)
  {
    unsigned int pack, pal;
    unsigned code;

    code = nametab[tilex_ty& 0x1f] << 3;
    pal  = coltab[code];
    pack = pattab[code];
    TileNormBgM2(cells_dx, pack, pal);
  }
}

/* Draw a scanline */
static void DrawDisplayM2(int scanline)
{
  struct PicoVideo *pv = &Pico.video;
  u8 *nametab, *coltab, *pattab;
  int tilex, dx, cells;
  int cellskip = 0; // XXX
  int maxcells = 32;

  // name, color, pattern table:
  nametab = PicoMem.vramb + ((pv->reg[2]<<10) & 0x3c00);
  coltab  = PicoMem.vramb + ((pv->reg[3]<< 6) & 0x2000);
  pattab  = PicoMem.vramb + ((pv->reg[4]<<11) & 0x2000);

  nametab += ((scanline>>3) << 5);
  coltab  += ((scanline>>6) <<11) + (scanline & 0x7);
  pattab  += ((scanline>>6) <<11) + (scanline & 0x7);

  tilex = cellskip & 0x1f;
  cells = maxcells - cellskip;
  dx = (cellskip << 3) + line_offset + 8;

  // tiles
  if (!(pv->debug_p & PVD_KILL_B))
    DrawStripM2(nametab, coltab, pattab, dx | (cells << 16), tilex | (scanline << 16));

  // sprites
  if (!(pv->debug_p & PVD_KILL_S_LO))
    DrawSpritesM2(scanline);
}


/* Common/global */
/*===============*/

static void FinalizeLineRGB555SMS(int line);

void PicoFrameStartSMS(void)
{
  int lines = 192, columns = 256, coffs;
  skip_next_line = 0;
  screen_offset = 24;
  Pico.est.rendstatus = PDRAW_32_COLS;

  switch ((Pico.video.reg[0]&0x06) | (Pico.video.reg[1]&0x18)) {
  // SMS2 only 224/240 line modes, e.g. Micro Machines
  case 0x06|0x08:
      screen_offset = 0;
      lines = 240;
      break;
  case 0x06|0x10:
      screen_offset = 8;
      lines = 224;
      break;
  }
  if (PicoIn.opt & POPT_EN_SOFTSCALE) {
    line_offset = 0;
    columns = 320;
  } else
    line_offset = PicoIn.opt & POPT_DIS_32C_BORDER ? 0 : 32;

  coffs = line_offset;
  if (FinalizeLineSMS == FinalizeLineRGB555SMS)
    line_offset = 0 /* done in FinalizeLine */;

  if (Pico.est.rendstatus != rendstatus_old || lines != rendlines) {
    emu_video_mode_change(screen_offset, lines, coffs, columns);
    rendstatus_old = Pico.est.rendstatus;
    rendlines = lines;
  }

  Pico.est.HighCol = HighColBase + screen_offset * HighColIncrement;
  Pico.est.DrawLineDest = (char *)DrawLineDestBase + screen_offset * DrawLineDestIncrement;
}

void PicoLineSMS(int line)
{
  if (skip_next_line > 0) {
    skip_next_line--;
    return;
  }

  if (PicoScanBegin != NULL)
    skip_next_line = PicoScanBegin(line + screen_offset);

  // Draw screen:
  BackFill(Pico.video.reg[7] & 0x0f, 0, &Pico.est);
  if (Pico.video.reg[1] & 0x40) {
    if (Pico.video.reg[0] & 0x04) DrawDisplayM4(line);
    else                          DrawDisplayM2(line);
  }

  if (FinalizeLineSMS != NULL)
    FinalizeLineSMS(line);

  if (PicoScanEnd != NULL)
    skip_next_line = PicoScanEnd(line + screen_offset);

  Pico.est.HighCol += HighColIncrement;
  Pico.est.DrawLineDest = (char *)Pico.est.DrawLineDest + DrawLineDestIncrement;
}

/* Fixed palette for TMS9918 modes */
static u16 tmspal[32] = {
 0x00,0x00,0x08,0x0c,0x10,0x30,0x01,0x3c,0x02,0x03,0x05,0x0f,0x04,0x33,0x15,0x3f
};

void PicoDoHighPal555SMS(void)
{
  unsigned int *spal=(void *)PicoMem.cram;
  unsigned int *dpal=(void *)Pico.est.HighPal;
  unsigned int t;
  int i;

  Pico.m.dirtyPal = 0;
  if (!(Pico.video.reg[0] & 0x4))
    spal = (u32 *)tmspal;

  /* cram is always stored as shorts, even though real hardware probably uses bytes */
  if (PicoIn.AHW & PAHW_SMS) for (i = 0x20/2; i > 0; i--, spal++, dpal++) { 
    t = *spal;
#if defined(USE_BGR555)
    t = ((t & 0x00030003)<< 3) | ((t & 0x000c000c)<<6) | ((t & 0x00300030)<<9);
    t |= (t >> 2) | ((t >> 4) & 0x04210421);
#elif defined(USE_BGR565)
    t = ((t & 0x00030003)<< 3) | ((t & 0x000c000c)<<7) | ((t & 0x00300030)<<10);
    t |= (t >> 2) | ((t >> 4) & 0x08610861);
#else
    t = ((t & 0x00030003)<<14) | ((t & 0x000c000c)<<7) | ((t & 0x00300030)>>1);
    t |= (t >> 2) | ((t >> 4) & 0x08610861);
#endif
    *dpal = t;
  } else for (i = 0x20/2; i > 0; i--, spal++, dpal++) { // GG palette 4 bit/col
    t = *spal;
#if defined(USE_BGR555)
    t = ((t & 0x000f000f)<< 1) | ((t & 0x00f000f0)<<2) | ((t & 0x0f000f00)<<3);
    t |= (t >> 4) & 0x04210421;
#elif defined(USE_BGR565)
    t = ((t & 0x000f000f)<< 1) | ((t & 0x00f000f0)<<3) | ((t & 0x0f000f00)<<4);
    t |= (t >> 4) & 0x08610861;
#else
    t = ((t & 0x000f000f)<<12) | ((t & 0x00f000f0)<<3) | ((t & 0x0f000f00)>>7);
    t |= (t >> 4) & 0x08610861;
#endif
    *dpal = t;
  }
  Pico.est.HighPal[0xe0] = 0;
}

static void FinalizeLineRGB555SMS(int line)
{
  if (Pico.m.dirtyPal)
    PicoDoHighPal555SMS();

  // standard FinalizeLine can finish it for us,
  // with features like scaling and such
  FinalizeLine555(0, line, &Pico.est);
}

static void FinalizeLine8bitSMS(int line)
{
  u8 *pd = Pico.est.DrawLineDest + line_offset;
  u8 *ps = Pico.est.HighCol + line_offset + 8;

  if (DrawLineDestIncrement) {
    if (PicoIn.opt & POPT_EN_SOFTSCALE)
      rh_upscale_nn_4_5(pd, 320, ps, 256, 256, f_nop);
    else if (pd != ps)
      memcpy(pd, ps, 256);
  }
}

void PicoDrawSetOutputSMS(pdso_t which)
{
  switch (which)
  {
    case PDF_8BIT:   FinalizeLineSMS = FinalizeLine8bitSMS; break;
    case PDF_RGB555: FinalizeLineSMS = FinalizeLineRGB555SMS; break;
    default:         FinalizeLineSMS = NULL;
                     PicoDrawSetInternalBuf(Pico.est.Draw2FB, 328); break;
  }
  rendstatus_old = -1;
}

// vim:shiftwidth=2:ts=2:expandtab
