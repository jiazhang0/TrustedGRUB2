/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2013 Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

/* All tests need to include test.h for GRUB testing framework.  */
#include <grub/test.h>
#include <grub/dl.h>
#include <grub/video.h>
#include <grub/lib/crc.h>
#include <grub/mm.h>

GRUB_MOD_LICENSE ("GPLv3+");

static int ctr;
static int nchk;
static char *basename;
static const grub_uint32_t *checksums;
static struct grub_video_mode_info capt_mode_info;

#ifdef GRUB_MACHINE_EMU
#include <grub/emu/misc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

struct bmp_header
{
  grub_uint8_t magic[2];
  grub_uint32_t filesize;
  grub_uint32_t reserved;
  grub_uint32_t bmp_off;
  grub_uint32_t head_size;
  grub_uint16_t width;
  grub_uint16_t height;
  grub_uint16_t planes;
  grub_uint16_t bpp;
} __attribute__ ((packed));

static void
grub_video_capture_write_bmp (const char *fname,
			      void *ptr,
			      const struct grub_video_mode_info *mode_info)
{
  int fd = open (fname, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  struct bmp_header head;

  if (fd < 0)
    {
      grub_printf (_("cannot open `%s': %s"),
		   fname, strerror (errno));
    }

  grub_memset (&head, 0, sizeof (head));

  head.magic[0] = 'B';
  head.magic[1] = 'M';

  if (mode_info->mode_type & GRUB_VIDEO_MODE_TYPE_RGB)
    {
      head.filesize = grub_cpu_to_le32 (sizeof (head) + mode_info->width * mode_info->height * 3);
      head.bmp_off = grub_cpu_to_le32 (sizeof (head));
      head.bpp = grub_cpu_to_le16_compile_time (24);
    }
  else
    {
      head.filesize = grub_cpu_to_le32 (sizeof (head) + 3 * 256 + mode_info->width * mode_info->height);
      head.bmp_off = grub_cpu_to_le32 (sizeof (head) + 3 * 256);
      head.bpp = grub_cpu_to_le16_compile_time (8);
    }
  head.head_size = grub_cpu_to_le32 (sizeof (head) - 14);
  head.width = grub_cpu_to_le16 (mode_info->width);
  head.height = grub_cpu_to_le16 (mode_info->height);
  head.planes = grub_cpu_to_le16_compile_time (1);

  head.width = grub_cpu_to_le16 (mode_info->width);
  head.height = grub_cpu_to_le16 (mode_info->height);

  write (fd, &head, sizeof (head));

  if (!(mode_info->mode_type & GRUB_VIDEO_MODE_TYPE_RGB))
    {
      struct grub_video_palette_data palette_data[256];
      int i;
      int palette_len = mode_info->number_of_colors;
      grub_memset (palette_data, 0, sizeof (palette_data));
      if (palette_len > 256)
	palette_len = 256;
      grub_video_get_palette (0, palette_len, palette_data);
      for (i = 0; i < 256; i++)
	{
	  grub_uint8_t r, g, b;
	  r = palette_data[i].r;
	  g = palette_data[i].g;
	  b = palette_data[i].b;

	  write (fd, &b, 1);
	  write (fd, &g, 1);
	  write (fd, &r, 1);
	}
    }

  /* This does essentialy the same as some fbblit functions yet using
     them would mean testing them against themselves so keep this
     implemetation separate.  */
  switch (mode_info->bytes_per_pixel)
    {
    case 4:
      {
	grub_uint8_t *buffer = xmalloc (mode_info->width * 3);
	grub_uint32_t rmask = ((1 << mode_info->red_mask_size) - 1);
	grub_uint32_t gmask = ((1 << mode_info->green_mask_size) - 1);
	grub_uint32_t bmask = ((1 << mode_info->blue_mask_size) - 1);
	int rshift = mode_info->red_field_pos;
	int gshift = mode_info->green_field_pos;
	int bshift = mode_info->blue_field_pos;
	int mulrshift = (8 - mode_info->red_mask_size);
	int mulgshift = (8 - mode_info->green_mask_size);
	int mulbshift = (8 - mode_info->blue_mask_size);
	int y;

	for (y = mode_info->height - 1; y >= 0; y--)
	  {
	    grub_uint32_t *iptr = (grub_uint32_t *) ((grub_uint8_t *) ptr + mode_info->pitch * y);
	    int x;
	    grub_uint8_t *optr = buffer;
	    for (x = 0; x < (int) mode_info->width; x++)
	      {
		grub_uint32_t val = *iptr++;
		*optr++ = ((val >> bshift) & bmask) << mulbshift;
		*optr++ = ((val >> gshift) & gmask) << mulgshift;
		*optr++ = ((val >> rshift) & rmask) << mulrshift;
	      }
	    write (fd, buffer, mode_info->width * 3);
	  }
	grub_free (buffer);
	break;
      }
    case 3:
      {
	grub_uint8_t *buffer = xmalloc (mode_info->width * 3);
	grub_uint32_t rmask = ((1 << mode_info->red_mask_size) - 1);
	grub_uint32_t gmask = ((1 << mode_info->green_mask_size) - 1);
	grub_uint32_t bmask = ((1 << mode_info->blue_mask_size) - 1);
	int rshift = mode_info->red_field_pos;
	int gshift = mode_info->green_field_pos;
	int bshift = mode_info->blue_field_pos;
	int mulrshift = (8 - mode_info->red_mask_size);
	int mulgshift = (8 - mode_info->green_mask_size);
	int mulbshift = (8 - mode_info->blue_mask_size);
	int y;

	for (y = mode_info->height - 1; y >= 0; y--)
	  {
	    grub_uint8_t *iptr = ((grub_uint8_t *) ptr + mode_info->pitch * y);
	    int x;
	    grub_uint8_t *optr = buffer;
	    for (x = 0; x < (int) mode_info->width; x++)
	      {
		grub_uint32_t val = 0;
#ifdef GRUB_CPU_WORDS_BIGENDIAN
		val |= *iptr++ << 16;
		val |= *iptr++ << 8;
		val |= *iptr++;
#else
		val |= *iptr++;
		val |= *iptr++ << 8;
		val |= *iptr++ << 16;
#endif
		*optr++ = ((val >> bshift) & bmask) << mulbshift;
		*optr++ = ((val >> gshift) & gmask) << mulgshift;
		*optr++ = ((val >> rshift) & rmask) << mulrshift;
	      }
	    write (fd, buffer, mode_info->width * 3);
	  }
	grub_free (buffer);
	break;
      }
    case 2:
      {
	grub_uint8_t *buffer = xmalloc (mode_info->width * 3);
	grub_uint16_t rmask = ((1 << mode_info->red_mask_size) - 1);
	grub_uint16_t gmask = ((1 << mode_info->green_mask_size) - 1);
	grub_uint16_t bmask = ((1 << mode_info->blue_mask_size) - 1);
	int rshift = mode_info->red_field_pos;
	int gshift = mode_info->green_field_pos;
	int bshift = mode_info->blue_field_pos;
	int mulrshift = (8 - mode_info->red_mask_size);
	int mulgshift = (8 - mode_info->green_mask_size);
	int mulbshift = (8 - mode_info->blue_mask_size);
	int y;

	for (y = mode_info->height - 1; y >= 0; y--)
	  {
	    grub_uint16_t *iptr = (grub_uint16_t *) ((grub_uint8_t *) ptr + mode_info->pitch * y);
	    int x;
	    grub_uint8_t *optr = buffer;
	    for (x = 0; x < (int) mode_info->width; x++)
	      {
		grub_uint16_t val = *iptr++;
		*optr++ = ((val >> bshift) & bmask) << mulbshift;
		*optr++ = ((val >> gshift) & gmask) << mulgshift;
		*optr++ = ((val >> rshift) & rmask) << mulrshift;
	      }
	    write (fd, buffer, mode_info->width * 3);
	  }
	grub_free (buffer);
	break;
      }
    case 1:
      {
	int y;

	for (y = mode_info->height - 1; y >= 0; y--)
	  write (fd, ((grub_uint8_t *) ptr + mode_info->pitch * y), mode_info->width);
	break;
      }
    }
  close (fd);
}

#endif

static const char *
get_modename (void)
{
  static char buf[40];
  if (capt_mode_info.mode_type & GRUB_VIDEO_MODE_TYPE_INDEX_COLOR)
    {
      grub_snprintf (buf, sizeof (buf), "i%d", capt_mode_info.number_of_colors);
      return buf;
    }
  if (capt_mode_info.red_field_pos == 0)
    {
      grub_snprintf (buf, sizeof (buf), "bgra%d%d%d%d", capt_mode_info.blue_mask_size,
		     capt_mode_info.green_mask_size,
		     capt_mode_info.red_mask_size,
		     capt_mode_info.reserved_mask_size);
      return buf;
    }
  grub_snprintf (buf, sizeof (buf), "rgba%d%d%d%d", capt_mode_info.red_mask_size,
		 capt_mode_info.green_mask_size,
		 capt_mode_info.blue_mask_size,
		 capt_mode_info.reserved_mask_size);
  return buf;
}

//#define GENERATE_MODE 1

#if defined (GENERATE_MODE) && defined (GRUB_MACHINE_EMU)
int genfd = -1;
#endif

static void
checksum (void)
{
  void *ptr;
  grub_uint32_t crc = 0;

  ptr = grub_video_capture_get_framebuffer ();

#ifdef GRUB_CPU_WORDS_BIGENDIAN
  switch (capt_mode_info.bytes_per_pixel)
    {
    case 1:
      crc = grub_getcrc32c (0, ptr, capt_mode_info.pitch
			    * capt_mode_info.height);
      break;
    case 2:
      {
	unsigned x, y, rowskip;
	grub_uint8_t *iptr = ptr;
	crc = 0;
	rowskip = capt_mode_info.pitch - capt_mode_info.width * 2;
	for (y = 0; y < capt_mode_info.height; y++)
	  {
	    for (x = 0; x < capt_mode_info.width; x++)
	      {
		crc = grub_getcrc32c (crc, iptr + 1, 1);
		crc = grub_getcrc32c (crc, iptr, 1);
		iptr += 2;
	      }
	    crc = grub_getcrc32c (crc, iptr, rowskip);
	    iptr += rowskip;
	  }
	break;
      }
    case 3:
      {
	unsigned x, y, rowskip;
	grub_uint8_t *iptr = ptr;
	crc = 0;
	rowskip = capt_mode_info.pitch - capt_mode_info.width * 3;
	for (y = 0; y < capt_mode_info.height; y++)
	  {
	    for (x = 0; x < capt_mode_info.width; x++)
	      {
		crc = grub_getcrc32c (crc, iptr + 2, 1);
		crc = grub_getcrc32c (crc, iptr + 1, 1);
		crc = grub_getcrc32c (crc, iptr, 1);
		iptr += 3;
	      }
	    crc = grub_getcrc32c (crc, iptr, rowskip);
	    iptr += rowskip;
	  }
	break;
      }
    case 4:
      {
	unsigned x, y, rowskip;
	grub_uint8_t *iptr = ptr;
	crc = 0;
	rowskip = capt_mode_info.pitch - capt_mode_info.width * 4;
	for (y = 0; y < capt_mode_info.height; y++)
	  {
	    for (x = 0; x < capt_mode_info.width; x++)
	      {
		crc = grub_getcrc32c (crc, iptr + 3, 1);
		crc = grub_getcrc32c (crc, iptr + 2, 1);
		crc = grub_getcrc32c (crc, iptr + 1, 1);
		crc = grub_getcrc32c (crc, iptr, 1);
		iptr += 4;
	      }
	    crc = grub_getcrc32c (crc, iptr, rowskip);
	    iptr += rowskip;
	  }
	break;
      }
    }
#else
  crc = grub_getcrc32c (0, ptr, capt_mode_info.pitch * capt_mode_info.height);
#endif
  if (!checksums || ctr >= nchk)
    {
      grub_test_assert (0, "Unexpected checksum %s_%dx%dx%s:%d: 0x%x",
			basename, 			
			capt_mode_info.width,
			capt_mode_info.height, get_modename (), ctr, crc);
    }
  else if (crc != checksums[ctr])
    {
      grub_test_assert (0, "Checksum %s_%dx%dx%s:%d failed: 0x%x vs 0x%x",
			basename,
			capt_mode_info.width,
			capt_mode_info.height, get_modename (),
			ctr, crc, checksums[ctr]);
    }
  else
    {
#if !(defined (GENERATE_MODE) && defined (GRUB_MACHINE_EMU))
      ctr++;
      return;
#endif
    }
#ifdef GRUB_MACHINE_EMU
  char *name = grub_xasprintf ("%s_%dx%dx%s_%d.bmp", basename, 
			       capt_mode_info.width,
			       capt_mode_info.height, get_modename (),
			       ctr);
  grub_video_capture_write_bmp (name, ptr, &capt_mode_info);
#endif

#if defined (GENERATE_MODE) && defined (GRUB_MACHINE_EMU)
  if (genfd >= 0)
    {
      char buf[20];
      grub_snprintf (buf, sizeof (buf), "0x%x, ", crc);
      write (genfd, buf, grub_strlen (buf));
    }
#endif

  ctr++;
}

struct checksum_desc
{
  const char *name;
  unsigned width;
  unsigned height;
  unsigned mode_type;
  unsigned number_of_colors;
  unsigned bpp;
  unsigned bytes_per_pixel;
  unsigned red_field_pos;
  unsigned red_mask_size;
  unsigned green_field_pos;
  unsigned green_mask_size;
  unsigned blue_field_pos;
  unsigned blue_mask_size;
  unsigned reserved_field_pos;
  unsigned reserved_mask_size;
  const grub_uint32_t *checksums;
  int nchk;
};

const struct checksum_desc checksum_table[] = {
#include "checksums.c"
};

void
grub_video_checksum (const char *basename_in)
{
  unsigned i;

  grub_video_get_info (&capt_mode_info);

#if defined (GENERATE_MODE) && defined (GRUB_MACHINE_EMU)
  if (genfd < 0)
    genfd = open ("checksums.c", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (genfd >= 0)
    {
      char buf[400];

      grub_snprintf (buf, sizeof (buf), "\", %d, %d, 0x%x, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d /* %dx%dx%s */, (grub_uint32_t []) { ",
		     capt_mode_info.width,
		     capt_mode_info.height,
		     capt_mode_info.mode_type,
		     capt_mode_info.number_of_colors,
		     capt_mode_info.bpp,
		     capt_mode_info.bytes_per_pixel,
		     capt_mode_info.red_field_pos,
		     capt_mode_info.red_mask_size,
		     capt_mode_info.green_field_pos,
		     capt_mode_info.green_mask_size,
		     capt_mode_info.blue_field_pos,
		     capt_mode_info.blue_mask_size,
		     capt_mode_info.reserved_field_pos,
		     capt_mode_info.reserved_mask_size,
		     capt_mode_info.width,
		     capt_mode_info.height, get_modename ());

      write (genfd, "  { \"", 5);
      write (genfd, basename_in, grub_strlen (basename_in));
      write (genfd, buf, grub_strlen (buf));
    }
#endif

  basename = grub_strdup (basename_in);
  nchk = 0;
  checksums = 0;
  /* FIXME: optimize this.  */
  for (i = 0; i < ARRAY_SIZE (checksum_table); i++)
    if (grub_strcmp (checksum_table[i].name, basename_in) == 0
	&& capt_mode_info.width == checksum_table[i].width
	&& capt_mode_info.height == checksum_table[i].height
	&& capt_mode_info.mode_type == checksum_table[i].mode_type
	&& capt_mode_info.number_of_colors == checksum_table[i].number_of_colors
	&& capt_mode_info.bpp == checksum_table[i].bpp
	&& capt_mode_info.bytes_per_pixel == checksum_table[i].bytes_per_pixel
	&& capt_mode_info.red_field_pos == checksum_table[i].red_field_pos
	&& capt_mode_info.red_mask_size == checksum_table[i].red_mask_size
	&& capt_mode_info.green_field_pos == checksum_table[i].green_field_pos
	&& capt_mode_info.green_mask_size == checksum_table[i].green_mask_size
	&& capt_mode_info.blue_field_pos == checksum_table[i].blue_field_pos
	&& capt_mode_info.blue_mask_size == checksum_table[i].blue_mask_size
	&& capt_mode_info.reserved_field_pos == checksum_table[i].reserved_field_pos
	&& capt_mode_info.reserved_mask_size == checksum_table[i].reserved_mask_size)
      {
	nchk = checksum_table[i].nchk;
	checksums = checksum_table[i].checksums;
	break;
      }

  ctr = 0;
  grub_video_capture_refresh_cb = checksum;
}

void
grub_video_checksum_end (void)
{
#if defined (GENERATE_MODE) && defined (GRUB_MACHINE_EMU)
  if (genfd >= 0)
    {
      char buf[40];
      grub_snprintf (buf, sizeof (buf), "}, %x },\n", ctr);
      write (genfd, buf, grub_strlen (buf));
    }
#endif
  grub_test_assert (ctr == nchk, "Not enough checksums %s_%dx%dx%s: %d vs %d",
		    basename,
		    capt_mode_info.width,
		    capt_mode_info.height, get_modename (),
		    ctr, nchk);
  grub_free (basename);
  basename = 0;
  nchk = 0;
  checksums = 0;
  ctr = 0;
  grub_video_capture_refresh_cb = 0;
}
