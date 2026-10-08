#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef void (*muse_link_req_cb)(void *, int, const uint8_t *, size_t, bool);
bool muse_link_hatch_linked(void);
bool muse_link_req_ready(void);
int64_t muse_link_req_open(const char *,const char *,const char *const *,bool,muse_link_req_cb,void *);
bool muse_link_req_send(int64_t,const void *,size_t,bool,int);
void muse_link_req_cancel(int64_t);
