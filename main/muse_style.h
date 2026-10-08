#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MUSE_STYLE_CUTE, MUSE_STYLE_ABSTRACT, MUSE_STYLE_FEMALE, MUSE_STYLE_ROUND, MUSE_STYLE_COUNT };
void muse_style_init(void);
unsigned muse_style_get(void);
bool muse_style_set(unsigned style);
bool muse_style_worker_start(void);
void muse_style_worker_stop(void);
unsigned muse_style_request_next(void);

#ifdef __cplusplus
}
#endif
