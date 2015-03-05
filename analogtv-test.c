//  Copyright © 2015 David Caldwell <david@porkrind.org> -*- compile-command: "cc -o analogtv-test analogtv-test.c thread.c analogtv.c -Wall $(pkg-config --cflags --libs gdlib)"; -*-

#include "analogtv.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <gd.h>

struct framebuffer *fb_alloc(unsigned width, unsigned height)
{
    struct framebuffer *fb = calloc(sizeof(struct framebuffer), 1);
    fb->width = width;
    fb->height = height;
    fb->bytes_per_line = width * 4;
    fb->pixels = malloc(width*height*4);
    return fb;
}
void fb_free(struct framebuffer *fb)
{
    if (fb) {
        free(fb->pixels);
        free(fb);
    }
}

gdImagePtr gd_image_from_fb(struct framebuffer *fb)
{
    gdImagePtr image = gdImageCreateTrueColor(fb->width, fb->height);
    uint32_t (*pix)[fb->height][fb->width] = fb->pixels;
    for (unsigned y=0; y<fb->height; y++)
        for (unsigned x=0; x<fb->width; x++)
            image->tpixels[y][x] = (int)(*pix)[y][x];
    return image;
}

void save_png(char *filename, gdImagePtr image)
{
    char *data = NULL;
    FILE *out = fopen(filename, "wb");
    if (!out) goto fail;
    int size;
    data = (char *) gdImagePngPtr(image, &size);
    if (!data) goto fail;
    if (fwrite(data, 1, size, out) != size)
        goto fail;
  fail:
    if (out)
        fclose(out);
    gdFree(data);
}

uint8_t text_screen_ram[] = {
#include "text-screen.h"
};

uint8_t hgr_screen_ram[] = {
#include "hgr-screen.h"
};

uint8_t apple_2_plus_charset[] = {
    #include "apple2+.charset.h"
};

unsigned text_line_offset(unsigned y) {
    return (y&7)<<7 | ((y&0x18)>>3) * 40;
};

unsigned hgr_line_offset(unsigned y) {
    return (y&7)<<10 | text_line_offset(y>>3);
};

int main(int argc, char **argv)
{
    analogtv *atv = analogtv_allocate(960, 720, (struct framebuffer_driver) { .alloc=fb_alloc, .free=fb_free });
    assert(atv);

    analogtv_input *input = analogtv_input_allocate();
    assert(input);

    analogtv_reception reception = {};
    reception.input = input;
    reception.level = 1.0;

    analogtv_set_defaults(atv);
    atv->squish_control=0.05;

    struct {
        bool graphics;
        bool hires;
        bool mixed;
        unsigned page;
    } video_mode = { .graphics=true, .hires=true, .mixed=false, .page=0 };

    bool flash_state = true; //(frame / frames__second / flash_period) & 1;

    if (argc >= 6) {
        video_mode.graphics    = !!strtoul(argv[1], NULL, 0);
        video_mode.hires       = !!strtoul(argv[2], NULL, 0);
        video_mode.mixed       = !!strtoul(argv[3], NULL, 0);
        video_mode.page        = !!strtoul(argv[4], NULL, 0);
        flash_state            = !!strtoul(argv[5], NULL, 0);
    }
    printf("graphics=%d, hires=%d, mixed=%d, page=%d, flash_state=%d\n", video_mode.graphics, video_mode.hires, video_mode.mixed, video_mode.page, flash_state);

    analogtv_setup_sync(input, video_mode.graphics/*do_cb*/, false/*do_ssavi*/);

    analogtv_setup_frame(atv);

    uint8_t *ram = video_mode.hires ? hgr_screen_ram : text_screen_ram;
    unsigned text_start = ((unsigned[]){0x0400, 0x0800})[video_mode.page]; // text or lo-res
    unsigned hgr_start  = ((unsigned[]){0x2000, 0x4000})[video_mode.page]; // hi-res

    // Fill input
    for (unsigned textrow=0; textrow<24; textrow++) {
        for (unsigned row=textrow*8; row<textrow*8+8; row++) {
            /* First we generate the pattern that the video circuitry shifts out
               of memory. It has a 14.something MHz dot clock, equal to 4 times
               the color burst frequency. So each group of 4 bits defines a color.
               Each character position, or byte in hires, defines 14 dots, so odd
               and even bytes have different color spaces. So, pattern[0..600]
               gets the dots for one scan line. */

            signed char *pp=&input->signal[row+ANALOGTV_TOP+4][ANALOGTV_PIC_START+100];
            if (video_mode.graphics && !video_mode.hires && (row<160 || !video_mode.mixed)) { // lores
                for (int x=0; x<40; x++) {
                    uint8_t c = ram[text_start + text_line_offset(textrow) + x];
                    uint8_t nib=c >> (((row/4)&1)*4) & 0xf;
                    /* The low or high nybble was shifted out one bit at a time. */
                    for (unsigned i=0; i<14; i++) {
                        *pp = (((nib>>((x*14+i)&3))&1)
                               ?ANALOGTV_WHITE_LEVEL
                               :ANALOGTV_BLACK_LEVEL);
                        pp++;
                    }
                }
            } else { // hires and text
                bool text = !video_mode.graphics || row>160 && video_mode.mixed;
                unsigned row_addr = text ? text_start + text_line_offset(textrow)
                                         : hgr_start  + hgr_line_offset(row);
                // printf("Row address %3d: %04x\n", row, row_addr);
                /* Emulate the mysterious pink line, due to a bit getting
                   stuck in a shift register between the end of the last
                   row and the beginning of this one. */
                if ((ram[row_addr +  0] & 0x80) &&
                    (ram[row_addr + 39] & 0x40)) {
                    pp[-1]=ANALOGTV_WHITE_LEVEL;
                }

                for (unsigned xs=0; xs<40; xs++) {
                    uint8_t seg = ram[row_addr+xs];
                    if (text) {
                        uint8_t ch = seg, y = row&7;;
                        //seg = charset_scanline(seg, row&7);
                        bool inverse = (ch & 0xc0) == 0 || // Inverse
                                       (ch & 0xc0) == 0x40 && flash_state; // Flash
                        seg = apple_2_plus_charset[(ch & 0x3f ^ 0x20)*8 + y] ^ (inverse ? 0x7f : 0);
                    }


                    int shift=(seg&0x80)?-1:0; // apple2.c has ?0:1 but that gives the wrong colors.

                    /* Each of the low 7 bits in hires mode corresponded to 2 dot
                       clocks, shifted by one if the high bit was set. */
                    for (unsigned i=0; i<7; i++) {
                        pp[shift+1] = pp[shift] = (((seg>>i)&1)
                                                   ?ANALOGTV_WHITE_LEVEL
                                                   :ANALOGTV_BLACK_LEVEL);
                        pp+=2;
                    }
                }
            }
        }
    }

    const analogtv_reception *recs = &reception; // fake array of receptions
    analogtv_draw(atv, 0.02, &recs, 1);

    save_png("out.png", gd_image_from_fb(atv->framebuffer));

    analogtv_release(atv);

    return 0;
}
