/* Portable image records, shared ownership, editing and native raster output. */
#include "../src/internal.h"
#include "stb_image_write.h"
#include <math.h>
#include <stdio.h>
static int checks, failures;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; fprintf(stderr,"%d: %s\n",__LINE__,#c); } } while(0)
static void write_bytes(void *context, void *bytes, int n) { fc_buf_putn(context, bytes, (size_t)n); }
static const unsigned char jpeg[] = {
    255,216,255,224,0,16,74,70,73,70,0,1,1,0,0,1,0,1,0,0,255,219,0,132,
    0,2,1,1,1,1,1,2,1,1,1,2,2,2,2,2,4,3,2,2,2,2,5,4,
    4,3,4,6,5,6,6,6,5,6,6,6,7,9,8,6,7,9,7,6,6,8,11,8,
    9,10,10,10,10,10,6,8,11,12,11,10,12,9,10,10,10,1,2,2,2,2,2,2,
    5,3,3,5,10,7,6,7,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
    10,10,10,10,10,10,10,10,10,10,255,192,0,17,8,0,2,0,2,3,1,17,0,2,
    17,1,3,17,1,255,196,1,162,0,0,1,5,1,1,1,1,1,1,0,0,0,0,0,
    0,0,0,1,2,3,4,5,6,7,8,9,10,11,16,0,2,1,3,3,2,4,3,5,
    5,4,4,0,0,1,125,1,2,3,0,4,17,5,18,33,49,65,6,19,81,97,7,34,
    113,20,50,129,145,161,8,35,66,177,193,21,82,209,240,36,51,98,114,130,9,10,22,23,
    24,25,26,37,38,39,40,41,42,52,53,54,55,56,57,58,67,68,69,70,71,72,73,74,
    83,84,85,86,87,88,89,90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,
    131,132,133,134,135,136,137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,
    169,170,178,179,180,181,182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,
    214,215,216,217,218,225,226,227,228,229,230,231,232,233,234,241,242,243,244,245,246,247,248,249,
    250,1,0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,1,2,3,4,5,
    6,7,8,9,10,11,17,0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119,0,
    1,2,3,17,4,5,33,49,6,18,65,81,7,97,113,19,34,50,129,8,20,66,145,161,
    177,193,9,35,51,82,240,21,98,114,209,10,22,36,52,225,37,241,23,24,25,26,38,39,
    40,41,42,53,54,55,56,57,58,67,68,69,70,71,72,73,74,83,84,85,86,87,88,89,
    90,99,100,101,102,103,104,105,106,115,116,117,118,119,120,121,122,130,131,132,133,134,135,136,
    137,138,146,147,148,149,150,151,152,153,154,162,163,164,165,166,167,168,169,170,178,179,180,181,
    182,183,184,185,186,194,195,196,197,198,199,200,201,202,210,211,212,213,214,215,216,217,218,226,
    227,228,229,230,231,232,233,234,242,243,244,245,246,247,248,249,250,255,218,0,12,3,1,0,
    2,17,3,17,0,63,0,253,7,253,128,254,21,252,48,215,191,97,47,130,154,230,185,240,
    223,65,189,189,189,248,73,225,185,239,47,46,244,120,36,150,121,95,75,183,103,145,221,148,
    150,102,98,73,98,73,36,146,107,252,137,250,79,98,241,121,47,210,87,141,178,252,190,164,
    168,225,232,230,249,149,58,116,233,183,8,83,132,49,149,163,8,66,17,180,99,8,197,40,
    198,49,73,69,36,146,73,30,133,47,15,184,11,136,105,71,53,205,114,156,54,35,21,136,
    74,173,90,181,104,82,169,86,173,90,158,253,74,149,42,78,14,83,156,228,220,167,57,55,
    41,73,182,219,109,179,255,217,
};
static const unsigned char pixels[] = {255,0,0,255, 0,255,0,128, 0,0,255,0, 255,255,0,255};
static void render(const FcDrawing *d, unsigned char *rgb) {
    memset(rgb,255,120*120*3);
    FcView view={0}; view.scale=1; view.bg=0xffffff;
    CHECK(fc_render_rgb(d,NULL,&view,rgb,120,120,120*3)==FC_OK);
}
static void check_pixel(unsigned char *rgb, int x, int y, int r,int g,int b) {
    unsigned char *p=rgb+(y*120+x)*3;
    CHECK(abs(p[0]-r)<=2 && abs(p[1]-g)<=2 && abs(p[2]-b)<=2);
}
int main(void) {
    FcBuf png,jpg; fc_buf_init(&png);fc_buf_init(&jpg);
    CHECK(stbi_write_png_to_func(write_bytes,&png,2,2,4,pixels,8));
    fc_buf_putn(&jpg,(const char *)jpeg,sizeof jpeg);
    FcDrawing d,round,flat;fc_drawing_init(&d);fc_drawing_init(&round);fc_drawing_init(&flat);
    CHECK(fc_drawing_add_image(&d,png.p,png.n,20,30,40,0,0)==0);
    CHECK(d.nimages==1 && d.prims[0].kind==FC_IMAGE && d.prims[0].u.image.height==40);
    int w,h;fc_image_size(d.prims[0].u.image.bitmap,&w,&h);CHECK(w==2 && h==2);
    CHECK(fc_drawing_add_image(&d,jpg.p,jpg.n,80,20,10,20,1)==1);
    CHECK(d.nimages==2);
    CHECK(fc_drawing_add_image(&d,png.p,png.n,0,0,0,10,0)==2 && d.nimages==2);
    CHECK(d.prims[2].u.image.width==10);
    fc_drawing_remove(&d,2);fc_drawing_remove(&d,1);
    char *text=NULL,*again=NULL;size_t len=fc_emit(&d,&text,1);
    CHECK(strstr(text,"FJC IMAGE 20 30 40 40 0 0 1 0 ")!=NULL);
    CHECK(fc_parse(&round,text,len)==FC_OK && round.nprims==1 && round.ndiags==0);
    fc_emit(&round,&again,1);CHECK(!strcmp(text,again));free(again);
    CHECK(round.prims[0].u.image.bitmap==d.prims[0].u.image.bitmap);
    fc_emit(&round,&again,0);CHECK(strstr(again,"IMAGE")==NULL);free(again);
    FcStats stats;fc_stats(&d,NULL,&stats);
    CHECK(stats.by_kind[FC_IMAGE]==1 && stats.has_bbox && stats.bbox.min_x==20 && stats.bbox.max_y==70);
    CHECK(fc_hit_test(&d,NULL,30,40,1)==0 && fc_hit_test(&d,NULL,80,90,1)==-1);
    CHECK(fc_prim_handles(&d.prims[0])==4);
    FcPoint corner;CHECK(fc_prim_handle(&d.prims[0],2,&corner) && corner.x==60 && corner.y==70);
    unsigned char rgb[120*120*3];render(&d,rgb);
    check_pixel(rgb,22,32,255,0,0);check_pixel(rgb,58,32,127,255,127);
    check_pixel(rgb,22,68,255,255,255);check_pixel(rgb,58,68,255,255,0);
    fc_parse(&d,"LI 20 32 60 32 1\n",0);render(&d,rgb);check_pixel(rgb,30,32,0,0,128);
    fc_drawing_remove(&d,1);
    d.prims[0].u.image.opacity=.5;d.layers[0].alpha=.5;render(&d,rgb);
    check_pixel(rgb,22,32,255,191,191);
    d.prims[0].u.image.opacity=1;d.layers[0].alpha=0;render(&d,rgb);
    check_pixel(rgb,22,32,255,255,255);d.layers[0].alpha=1;
    /* Layer RGB changes must not recolor an image. */
    d.layers[0].rgb=0;render(&d,rgb);check_pixel(rgb,22,32,255,0,0);
    d.prims[0].u.image.rotation=90;render(&d,rgb);check_pixel(rgb,17,32,255,0,0);
    d.prims[0].u.image.rotation=0;d.prims[0].u.image.mirror=1;render(&d,rgb);
    check_pixel(rgb,17,32,255,0,0);
    /* Every resize keeps the opposite corner fixed, including rotation/mirror. */
    for(int mirror=0;mirror<2;++mirror) for(int rot=0;rot<360;rot+=37) for(int i=0;i<4;++i) {
        FcPrim p=round.prims[0];p.u.image.p=(FcPoint){80,80};p.u.image.rotation=rot;p.u.image.mirror=mirror;
        FcPoint c[4],after[4];fc_image_corners(&p,c);
        fc_prim_set_handle(&p,i,c[i].x+2,c[i].y+3);fc_image_corners(&p,after);
        CHECK(hypot(c[(i+2)%4].x-after[(i+2)%4].x,c[(i+2)%4].y-after[(i+2)%4].y)<1e-8);
    }
    /* Rotation/mirror composition transforms all corners like ordinary points. */
    for(int m=0;m<2;++m)for(int o=0;o<4;++o) {
        FcPrim p=round.prims[0];p.u.image.p=(FcPoint){100,100};p.u.image.rotation=23;p.u.image.mirror=1;
        FcPoint a[4],b[4];fc_image_corners(&p,a);fc_xform_prim(&p,o,m,200,200);fc_image_corners(&p,b);
        for(int i=0;i<4;++i) {fc_xform_point(&a[i],o,m,200,200);CHECK(hypot(a[i].x-b[i].x,a[i].y-b[i].y)<1e-8);}
    }
    CHECK(fc_expand(&round,&flat,NULL)==FC_OK && flat.nprims==1);
    fc_drawing_free(&round);fc_drawing_free(&d);
    render(&flat,rgb);check_pixel(rgb,22,32,255,0,0);fc_drawing_free(&flat);
    /* Bad records report an error; no partial image object is added. */
    const char *bad[]={"FJC IMAGE 0 0 1 1 0 0 1 0 !!!!\n", "FJC IMAGE 0 0 0 1 0 0 1 0 AAAA\n",
        "FJC IMAGE 0 0 1 1 0 2 1 0 AAAA\n", "FJC IMAGE nan 0 1 1 0 0 1 0 AAAA\n"};
    fc_drawing_init(&d);
    for(size_t i=0;i<sizeof bad/sizeof *bad;++i) {
        fc_drawing_clear(&d);fc_parse(&d,bad[i],0);
        CHECK(d.nprims==0 && d.ndiags==1 && d.diags[0].level==FC_DIAG_ERR);
    }
    /* Header limits are checked before allocating/decompressing pixel data. */
    char *oversize=malloc(png.n);memcpy(oversize,png.p,png.n);
    oversize[16]=0;oversize[17]=0;oversize[18]=32;oversize[19]=1; /* width 8193 */
    CHECK(fc_drawing_add_image(&d,oversize,png.n,0,0,10,10,0)<0);free(oversize);
    char *record=malloc(len+1);strcpy(record,text);
    char *payload=strrchr(record,' ')+1;payload[0]='!';
    fc_drawing_clear(&d);fc_parse(&d,record,0);CHECK(d.nprims==0 && d.ndiags==1);free(record);
    CHECK(fc_drawing_add_image(&d,"not an image",12,0,0,10,10,0)<0);
    CHECK(fc_drawing_add_image(&d,png.p,png.n,0,0,NAN,10,0)<0);
    CHECK(fc_drawing_add_image(&d,png.p,png.n,0,0,1e-8,10,0)<0);
    CHECK(fc_drawing_add_image(&d,png.p,png.n,0,0,10,10,16)<0);
    CHECK(fc_drawing_add_image(&d,png.p,png.n,-1,0,10,10,0)<0);
    CHECK(fc_drawing_add_image(&d,png.p,FC_IMAGE_MAX_BYTES+1,0,0,10,10,0)<0);
    /* Exercise resource eviction while another drawing still owns an old image. */
    CHECK(fc_parse(&d,text,len)==FC_OK);
    fc_drawing_init(&flat);CHECK(fc_expand(&d,&flat,NULL)==FC_OK);fc_drawing_free(&d);
    for(int i=0;i<12;++i) {
        unsigned char color[4]={(unsigned char)i,12,34,255};FcBuf buf;fc_buf_init(&buf);
        stbi_write_png_to_func(write_bytes,&buf,1,1,4,color,4);
        fc_drawing_init(&round);CHECK(fc_drawing_add_image(&round,buf.p,buf.n,0,0,10,10,0)==0);
        fc_drawing_free(&round);fc_buf_free(&buf);
    }
    render(&flat,rgb);check_pixel(rgb,22,32,255,0,0);
    free(text);fc_drawing_free(&flat);fc_buf_free(&png);fc_buf_free(&jpg);
    printf("%d/%d image checks passed\n",checks-failures,checks);return failures?1:0;
}
