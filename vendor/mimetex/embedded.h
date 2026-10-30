#ifndef LEO_MIMETEX_EMBEDDED_H
#define LEO_MIMETEX_EMBEDDED_H

/* Internal bridge. The caller serializes calls (mimeTeX has global state).
 * Pixels are owned by the caller, 0 = transparent, 255 = ink. */
int fc_mimetex_render(char *expression, int display, unsigned char **pixels,
                     int *width, int *height, int *baseline, float *em);

#endif
