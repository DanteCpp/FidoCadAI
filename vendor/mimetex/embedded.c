/* Keep mimeTeX's private functions and globals in one translation unit. No
 * CGI driver, file handlers, GIF encoder, browser or external TeX process. */
#define MIMETEX_EMBEDDED
#define FUNCSHARED static
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <strings.h>
#include <limits.h>
#include "mimetex.c"
#include "embedded.h"

int fc_mimetex_render(char *expression, int display, unsigned char **pixels,
                     int *width, int *height, int *baseline, float *em) {
    subraster *sp;
    int x, y;
    /* All equations use the largest built-in Computer Modern fonts, then the
     * consumer filters to its actual zoom. Layout is independent of zoom. */
    msgfp = NULL;
    msglevel = 0;
    fontnum = 0;
    fontsize = LARGESTSIZE;
    fonttable = aafonttable;
    issupersampling = isss = 0;
    isdisplaystyle = display ? 2 : 0;
    ispreambledollars = 1;
    isstring = isnomath = 0;
    recurlevel = scriptlevel = 0;
    magstep = 1;
    unitlength = 1;
    smashmargin = mathsmashmargin = 0;
    issmashdelta = 1;
    isexplicitsmash = isscripted = isdelimscript = 0;
    isnocatspace = blanksymspace = 0;
    warninglevel = 0;
    fc_mimetex_error = 0;
    fc_mimetex_array_gap = .75;
    daemonlevel = daemonlevel == INT_MAX ? 1 : daemonlevel + 1;
    mimeprep(expression);
    sp = rasterize(expression, LARGESTSIZE);
    if (!sp || !sp->image || fc_mimetex_error) {
        delete_subraster(sp);
        return 0;
    }
    *width = sp->image->width;
    *height = sp->image->height;
    *baseline = sp->baseline + 1;
    /* cmr600: 10pt at 600 dpi = 83 1/3 pixels/em. */
    *em = (float)fc_mimetex_em(LARGESTSIZE);
    *pixels = malloc((size_t)*width * (size_t)*height);
    if (!*pixels) {
        delete_subraster(sp);
        return 0;
    }
    for (y = 0; y < *height; ++y)
        for (x = 0; x < *width; ++x)
            (*pixels)[y * *width + x] = (unsigned char)(getpixel(sp->image, y, x) ? 255 : 0);
    delete_subraster(sp);
    return 1;
}
