/* Passport OS UTF-8 captions; preserves Chinese rather than converting to ASCII. */
#include "muse_chat_priv.h"
#include <string.h>
static void le(uint8_t *p,uint32_t v,int n){for(int i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
void muse_hatch_wav_header(uint8_t h[MUSE_HATCH_WAV_HEADER],uint32_t rate){
    memcpy(h,"RIFF",4);le(h+4,UINT32_MAX,4);memcpy(h+8,"WAVEfmt ",8);le(h+16,16,4);
    le(h+20,1,2);le(h+22,1,2);le(h+24,rate,4);le(h+28,rate*2,4);le(h+32,2,2);le(h+34,16,2);
    memcpy(h+36,"data",4);le(h+40,UINT32_MAX,4);
}
size_t muse_hatch_base64(const uint8_t *p,size_t n,char *o){
    static const char a[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t k=0;for(size_t i=0;i<n;i+=3){uint32_t v=(uint32_t)p[i]<<16;
        if(i+1<n)v|=(uint32_t)p[i+1]<<8;
        if(i+2<n)v|=p[i+2];
        o[k++]=a[v>>18];o[k++]=a[v>>12&63];o[k++]=i+1<n?a[v>>6&63]:'=';o[k++]=i+2<n?a[v&63]:'=';}
    return k;
}
void muse_hatch_tail_words(const char *src,char *out,size_t cap){
    if(!cap)return;
    size_t len=strlen(src);const char *p=src;
    if(len>=cap){p=src+len-cap+1;while(((unsigned char)*p&0xc0)==0x80)p++;}
    size_t n = strlen(p);
    if(n >= cap)n=cap-1;
    memcpy(out,p,n);out[n]=0;
}
/* Nine lines in the full-height reply panel: 26 half-width columns.
 * Return the next byte offset for manual paging; never split a UTF-8 glyph. */
static size_t utf8_bytes(const char *p) {
    unsigned char c = (unsigned char)*p;
    size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    for (size_t i = 1; i < n; i++)
        if (!p[i] || ((unsigned char)p[i] & 0xc0) != 0x80) return 1;
    return n;
}
size_t muse_hatch_reply_page(const char *text, size_t at, char *out, size_t cap) {
    if (!cap) return at;
    out[0] = 0;
    size_t len = strlen(text);
    if (at >= len) return len;
    while (at && ((unsigned char)text[at] & 0xc0) == 0x80) at--;
    size_t used = 0;
    unsigned col = 0, lines = 1;
    while (at < len) {
        size_t n = utf8_bytes(text + at);
        /* The UI font has wide Latin letters too (e.g. W/m). Count only
         * glyphs whose advance is <= 8 px as one half-width column. */
        static const char narrow[] = " !\"'()*,-./:;?I[\\]fijlrstxz{|}";
        unsigned width = n == 1 && strchr(narrow, text[at]) ? 1 : 2;
        bool newline = text[at] == '\n';
        bool wrap = !newline && col + width > 26;
        if (newline || wrap) {
            if (lines == 9) {
                if (newline) at++;
                break;
            }
            if (used + 1 >= cap) break;
            out[used++] = '\n'; lines++; col = 0;
            if (newline) { at++; continue; }
        }
        if (used + n >= cap) break;
        memcpy(out + used, text + at, n); used += n; at += n; col += width;
    }
    out[used] = 0;
    return at;
}
bool muse_hatch_caption_at(const char *src,size_t at,char *out,size_t cap){
    if (!*src || !cap) return false;
    size_t start = 0, next, len = strlen(src);
    if (at >= len) at = len - 1;
    do {
        next = muse_hatch_reply_page(src, start, out, cap);
        if (next > at || next <= start || next == len) break;
        start = next;
    } while (start < len);
    return out[0] != 0;
}
