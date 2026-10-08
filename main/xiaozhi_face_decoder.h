#pragma once
#include "lvgl.h"
#include "src/draw/lv_image_decoder_private.h"
#include "xiaozhi_face_rle.h"

/* Registered only while the conversation screen exists, under the LVGL lock.
 * Each draw session owns one scanline, including deferred draw tasks. */
static lv_image_decoder_t *face_decoder;
static lv_result_t face_decode_info(lv_image_decoder_t *decoder,lv_image_decoder_dsc_t *d,lv_image_header_t *header){
    (void)decoder;
    if(d->src_type!=LV_IMAGE_SRC_VARIABLE)return LV_RESULT_INVALID;
    const lv_image_dsc_t *im=(const lv_image_dsc_t *)d->src;
    if(im->header.cf!=LV_COLOR_FORMAT_A8||!(im->header.flags&LV_IMAGE_FLAGS_USER1))return LV_RESULT_INVALID;
    if(!im->header.w||im->header.w>160||!im->header.h||im->header.h>140)return LV_RESULT_INVALID;
    *header=im->header;header->flags=0;return LV_RESULT_OK;
}
static lv_result_t face_decode_open(lv_image_decoder_t *decoder,lv_image_decoder_dsc_t *d){
    (void)decoder;d->args.no_cache=true;
    d->user_data=lv_draw_buf_create(d->header.w,1,LV_COLOR_FORMAT_A8,LV_STRIDE_AUTO);
    return d->user_data?LV_RESULT_OK:LV_RESULT_INVALID;
}
static lv_result_t face_decode_area(lv_image_decoder_t *decoder,lv_image_decoder_dsc_t *d,
                                    const lv_area_t *full,lv_area_t *area){
    (void)decoder;
    if(area->y1==LV_COORD_MIN){*area=*full;area->y2=area->y1;}
    else {area->y1++;area->y2++;}
    if(area->y1>full->y2)return LV_RESULT_INVALID;
    const lv_image_dsc_t *im=(const lv_image_dsc_t *)d->src;lv_draw_buf_t *row=(lv_draw_buf_t *)d->user_data;
    unsigned w=lv_area_get_width(full);
    if(!row||!xz_face_rle_row(im->data,im->data_size,im->header.w,im->header.h,
                             area->y1,full->x1,w,row->data))return LV_RESULT_INVALID;
    row->header.w=w;d->decoded=row;return LV_RESULT_OK;
}
static void face_decode_close(lv_image_decoder_t *decoder,lv_image_decoder_dsc_t *d){
    (void)decoder;if(d->user_data)lv_draw_buf_destroy((lv_draw_buf_t *)d->user_data);d->user_data=NULL;d->decoded=NULL;
}
static void face_decoder_start(void){
    if(face_decoder)return;
    face_decoder=lv_image_decoder_create();if(!face_decoder)return;
    lv_image_decoder_set_info_cb(face_decoder,face_decode_info);
    lv_image_decoder_set_open_cb(face_decoder,face_decode_open);
    lv_image_decoder_set_get_area_cb(face_decoder,face_decode_area);
    lv_image_decoder_set_close_cb(face_decoder,face_decode_close);
}
static void face_decoder_stop(void){
    if(!face_decoder)return;
    /* Header entries contain the decoder pointer; never leave it dangling. */
    lv_image_header_cache_drop(NULL);
    lv_image_decoder_delete(face_decoder);face_decoder=NULL;
}
