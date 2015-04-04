/*
  
  bdf_tga.c
  
  
  
  Modes:
    BDF_BBX_MODE_MINIMAL 0
    BDF_BBX_MODE_MAX 1
    BDF_BBX_MODE_HEIGHT 2
  
  For all modes, default reference should be the baseline. 
  This is required for mode 0, but may be optional for 1 and 2
  
  If (x,y) is the user provided baseline point for the glyph, then 
  the decoding mus tbe start at
    (x..., y-h-descent)
    
  
  BDF_BBX_MODE_MINIMAL
    - exact space as intended by the font author
    - glyphs my overlap ("mj" with osb18)
    
  BDF_BBX_MODE_MAX
    - extra space may be added
    - glyphs do not overlap
  
  BDF_BBX_MODE_HEIGHT
    - extra space may be added
    - glyphs do not overlap
    
  
*/


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t tga_width;
static uint16_t tga_height;
static uint8_t *tga_data = NULL;

static uint8_t *tga_font;
static int glyph_cnt;
static int bits_per_0;
static int bits_per_1;
static int bits_per_char_width;
static int bits_per_char_height;
static int bits_per_char_x;
static int bits_per_char_y;
static int bits_per_delta_x;
static int char_width;
static int char_height;
static int char_descent;

int tga_get_char_width(void)
{
    return char_width;
}

int tga_get_char_height(void)
{
    return char_height;
}

int tga_init(uint16_t w, uint16_t h)
{
  tga_width = 0;
  tga_height = 0;
  if ( tga_data != NULL )
    free(tga_data);
  tga_data = (uint8_t *)malloc((size_t)w*(size_t)h*3);
  if ( tga_data == NULL )
    return 0;
  tga_width = w;
  tga_height = h;
  memset(tga_data, 255, tga_width*tga_height*3);
  return 1;
}

void tga_set_pixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  uint8_t *p;
  if ( y>= tga_height)
    return;
  if ( x>= tga_width)
    return;
  p = tga_data + (tga_height-y-1)*(size_t)tga_width*3 + (size_t)x*3;
  *p++ = b;
  *p++ = g;
  *p++ = r;
}

void tga_write_byte(FILE *fp, uint8_t byte)
{
  fputc(byte, fp);
}

void tga_write_word(FILE *fp, uint16_t word)
{
  tga_write_byte(fp, word&255);
  tga_write_byte(fp, word>>8);
}

void tga_save(const char *name)
{
  FILE *fp;
  fp = fopen(name, "wb");
  if ( fp != NULL )
  {
    tga_write_byte(fp, 0);		/* no ID */
    tga_write_byte(fp, 0);		/* no color map */
    tga_write_byte(fp, 2);		/* uncompressed true color */
    tga_write_word(fp, 0);		
    tga_write_word(fp, 0);		
    tga_write_byte(fp, 0);		
    tga_write_word(fp, 0);		/* x origin */
    tga_write_word(fp, 0);		/* y origin */
    tga_write_word(fp, tga_width);		/* width */
    tga_write_word(fp, tga_height);		/* height */
    tga_write_byte(fp, 24);		/* color depth */
    tga_write_byte(fp, 0);		
    fwrite(tga_data, tga_width*tga_height*3, 1, fp);
    tga_write_word(fp, 0);
    tga_write_word(fp, 0);
    tga_write_word(fp, 0);
    tga_write_word(fp, 0);
    fwrite("TRUEVISION-XFILE.", 18, 1, fp);
    fclose(fp);
  }
}

void tga_set_font(uint8_t *font)
{
    glyph_cnt = *font++;
    bits_per_0 = *font++;
    bits_per_1 = *font++;
    bits_per_char_width = *font++;
    bits_per_char_height = *font++;
    bits_per_char_x = *font++;
    bits_per_char_y = *font++;
    bits_per_delta_x = *font++;
    char_width = *font++;
    char_height = *font++;
    char_descent = *(int8_t *)font;
    font++;
    tga_font = font;
}

uint8_t *tga_get_glyph_data(uint8_t encoding)
{
  int i;
  uint8_t *font = tga_font;
  for( i = 0; i < glyph_cnt; i++ )
  {
    if ( font[0] == encoding )
    {
      return font;
    }
    font += font[1];
  }
  return NULL;
}

/* font decode */
struct tga_fd_struct
{
  unsigned target_x;
  unsigned target_y;
  
  unsigned x;						/* local coordinates, (0,0) is upper left */
  unsigned y;
  unsigned glyph_width;	
  unsigned glyph_height;

  
  const uint8_t *decode_ptr;			/* pointer to the compressed data */
  unsigned decode_bit_pos;			/* bitpos inside a byte of the compressed data */
  
  uint8_t bbx_x_max_bit_size;
  uint8_t bbx_y_max_bit_size;
  uint8_t bbx_w_max_bit_size;
  uint8_t bbx_h_max_bit_size;
  uint8_t dx_max_bit_size;
  
};
typedef struct tga_fd_struct tga_fd_t;

/* increment x and consider line wrap (inc y)*/
void tga_fd_inc(tga_fd_t *f)
{
  unsigned x = f->x;
  x++;
  if ( x == f->glyph_width )
  {
    x = 0;
    f->y++;
  }
  f->x = x;
}


unsigned tga_fd_get_unsigned_bits(tga_fd_t *f, unsigned cnt)
{
  unsigned val;
  unsigned bit_pos = f->decode_bit_pos;
  
  val = *(f->decode_ptr);
  
  val >>= bit_pos;
  if ( bit_pos + cnt >= 8 )
  {
    f->decode_ptr++;
    val |= *(f->decode_ptr) << (8-bit_pos);
    bit_pos -= 8;
  }
  val &= (1U<<cnt)-1;
  bit_pos += cnt;
  
  f->decode_bit_pos = bit_pos;
  return val;
}

/*
    2 bit --> cnt = 2
      -2,-1,0. 1

    3 bit --> cnt = 3
      -2,-1,0. 1
      -4,-3,-2,-1,0,1,2,3

      if ( x < 0 )
	r = bits(x-1)+1;
    else
	r = bits(x)+1;

*/
int tga_fd_get_signed_bits(tga_fd_t *t, int cnt)
{
  return (int)tga_fd_get_unsigned_bits(t, cnt) - ((1<<cnt)>>1);
}


void tga_fd_draw_pixel(tga_fd_t *f)
{
  tga_set_pixel(f->target_x+f->x, f->target_y+f->y, 0,0,0);
}

unsigned tga_fd_decode(tga_fd_t *f, uint8_t *glyph_data, int is_hints)
{
  unsigned a, b;
  unsigned i;
  int x, y;
  unsigned d = 0;
  //unsigned total;
  
  
  /* init decode algorithm */
  f->decode_ptr = glyph_data;
  f->decode_bit_pos = 0;
  
  f->decode_ptr += 1;
  /* read glyph info */
  //total = *f->decode_ptr;
  f->decode_ptr += 1;
  
  f->glyph_width = tga_fd_get_unsigned_bits(f, bits_per_char_width);
  f->glyph_height = tga_fd_get_unsigned_bits(f, bits_per_char_height);
  x = tga_fd_get_signed_bits(f, bits_per_char_x);
  y = tga_fd_get_signed_bits(f, bits_per_char_y);
  d = tga_fd_get_signed_bits(f, bits_per_delta_x);
  
  
  
  if ( f->glyph_width > 0 )
  {
    
    //printf("width: %d\n", f->glyph_width);
    //printf("height: %d\n", f->glyph_height);
    //printf("x: %d\n", x);
    //printf("y: %d\n", y);
    //printf("d: %d\n", d);
    
    f->target_x += x;
    f->target_y -= f->glyph_height ;
    f->target_y -=y ;
    
    
    /* reset local x/y position */
    f->x = 0;
    f->y = 0;
    
    //puts("");

    //printf("start decode ");

    /* decode glyph */
    for(;;)
    {
      a = tga_fd_get_unsigned_bits(f, bits_per_0);
      b = tga_fd_get_unsigned_bits(f, bits_per_1);
      //printf("[a=%u b=%u x=%u/%u y=%u]", a, b, f->x, f->glyph_width, f->y);
      do
      {
	for( i = 0; i < a; i++ )
	{
	  if ( is_hints )
	  {
	    tga_set_pixel(f->target_x+f->x, f->target_y+f->y, 0x0e0,0x0e0,0x0e0);
	  }
	  tga_fd_inc(f);
	}

	for( i = 0; i < b; i++ )
	{	
	  tga_fd_draw_pixel(f);
	  tga_fd_inc(f);
	}
	
      } while( tga_fd_get_unsigned_bits(f, 1) != 0 );
      
      if ( f->y >= f->glyph_height )
	break;
    }

    if ( is_hints )
    {
      // tga_set_pixel(f->target_x, f->target_y, 28,133,240);
    }
  }
  /*
  printf("\n");
  printf("[x=%u y=%u]\n", f->x, f->y);
  printf("cnt=%u total=%u\n", f->decode_ptr-glyph_data-2, total);
  printf("end decode\n");
  */
  return d;
}

unsigned tga_draw_glyph(unsigned x, unsigned y, uint8_t encoding, int is_hints)
{
  unsigned dx = 0;
  tga_fd_t f;
  f.target_x = x;
  f.target_y = y;
  uint8_t *glyph_data = tga_get_glyph_data(encoding);
  if ( glyph_data != NULL )
  {
    dx = tga_fd_decode(&f, glyph_data, is_hints);
    if ( is_hints )
    {
      tga_set_pixel(x+dx, y, 28,133,240);	/* orange: reference point */
      tga_set_pixel(x, y, 255,164,0);	/* blue: delta x (width) for this glyph */
    }
  }
  return dx;
}

unsigned tga_draw_string(unsigned x, unsigned y, const char *s, int is_hints)
{
  unsigned dx = 0;
  while( *s != '\0' )
  {
    dx += tga_draw_glyph(x+dx,y,*s, is_hints);
    s++;
  }
  return dx;
}


