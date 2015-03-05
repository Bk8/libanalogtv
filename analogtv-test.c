//  Copyright © 2015 David Caldwell <david@porkrind.org>

#include "analogtv.h"
#include "apple2-video-fancy.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <gd.h>

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

int main(int argc, char **argv)
{
    apple2_video_fancy_setup(1280, 720);

    struct video_mode video_mode = { .graphics=true, .hires=true, .mixed=false, .page=0 };

    char *filename = "out.png";
    if (argc >= 5) {
        video_mode.graphics    = !!strtoul(argv[1], NULL, 0);
        video_mode.hires       = !!strtoul(argv[2], NULL, 0);
        video_mode.mixed       = !!strtoul(argv[3], NULL, 0);
        video_mode.page        = !!strtoul(argv[4], NULL, 0);
    }
    if (argc >= 6)
        filename = argv[5];

    printf("graphics=%d, hires=%d, mixed=%d, page=%d\n", video_mode.graphics, video_mode.hires, video_mode.mixed, video_mode.page);

    struct framebuffer *fb = apple2_video_fancy_render(0,
                                                       30,
                                                       video_mode,
                                                       video_mode.hires ? hgr_screen_ram : text_screen_ram);

    save_png(filename, gd_image_from_fb(fb));

    apple2_video_fancy_cleanup();

    return 0;
}

