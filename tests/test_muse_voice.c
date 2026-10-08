#include "muse_chat_priv.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char *encoded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    char out[64];
    for (size_t i = 0; i < sizeof(plain)/sizeof(plain[0]); i++) {
        memset(out, 0, sizeof(out));
        size_t n = muse_hatch_base64((const uint8_t *)plain[i], strlen(plain[i]), out);
        assert(n == strlen(encoded[i]));
        assert(memcmp(out, encoded[i], n) == 0);
    }
    uint8_t h[44];
    muse_hatch_wav_header(h, 16000);
    assert(memcmp(h, "RIFF", 4) == 0 && memcmp(h+8, "WAVEfmt ", 8) == 0);
    assert(h[4] == 255 && h[7] == 255 && h[40] == 255 && h[43] == 255);
    assert(h[20] == 1 && h[22] == 1 && h[32] == 2 && h[34] == 16);
    assert(h[24] == 0x80 && h[25] == 0x3e && h[28] == 0 && h[29] == 0x7d);
    // UTF-8 captions must fit every capacity without incomplete code points,
    // including a four-byte emoji at the clipping boundary.
    const char *text = "Hello你好🙂世界";
    for (size_t cap = 1; cap <= strlen(text)+2; cap++) {
        memset(out, 0xa5, sizeof(out));
        muse_hatch_tail_words(text, out, cap);
        size_t n = strlen(out);
        assert(n < cap && (unsigned char)out[cap] == 0xa5);
        assert(!n || ((unsigned char)out[0] & 0xc0) != 0x80);
        assert(strcmp(out, text+strlen(text)-n) == 0);
    }
    char tiny[1] = {'x'};
    muse_hatch_tail_words(text, tiny, 0); assert(tiny[0] == 'x');
    assert(!muse_hatch_caption_at("", 0, out, sizeof(out)));
    assert(muse_hatch_caption_at(text, 0, out, sizeof(out)) && strcmp(out, text) == 0);
    char long_reply[1000], page[256];
    long_reply[0]=0;
    for (int i=0;i<60;i++) strcat(long_reply,"你好🙂A");
    size_t at=0, next; unsigned pages=0;
    do {
        next=muse_hatch_reply_page(long_reply,at,page,sizeof(page));
        assert(next>at && next<=strlen(long_reply));
        assert(((unsigned char)long_reply[next]&0xc0)!=0x80);
        assert(strstr(page,"你好") && strlen(page)<sizeof(page));
        at=next; pages++;
    } while(long_reply[at]);
    assert(pages>1);
    const char *wide="MMMMWWWWwhhhhnnnnoooo中文结果MMMMWWWWwhhhhnnnnoooo中文结果MMMMWWWWwhhhhnnnnoooo中文结果MMMMWWWWwhhhhnnnnoooo中文结果MMMMWWWWwhhhhnnnnoooo中文结果";
    char reconstructed[1000]={0}; at=0;
    do {
        next=muse_hatch_reply_page(wide,at,page,sizeof(page));
        assert(next>at);
        for (size_t i=0;page[i];i++) {
            if(page[i]!='\n') {size_t n=strlen(reconstructed);reconstructed[n]=page[i];reconstructed[n+1]=0;}
        }
        at=next;
    } while(wide[at]);
    assert(!strcmp(reconstructed,wide)); /* no hidden/skipped glyphs at a page boundary */
    for (size_t cap=1;cap<sizeof(out);cap++) {
        memset(out,0xa5,sizeof(out));
        next=muse_hatch_reply_page(text,0,out,cap);
        assert(strlen(out)<cap && (unsigned char)out[cap]==0xa5);
        assert(((unsigned char)text[next]&0xc0)!=0x80);
    }
    assert(muse_hatch_caption_at(long_reply,strlen(long_reply)-1,page,sizeof(page)) && strstr(page,"你好"));
    puts("Muse voice WAV/base64/UTF-8 boundaries and multi-page reply vectors: PASS");
}
