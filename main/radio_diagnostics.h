#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Allocation-free, task-safe counters; querying never wakes the screen or
 * mutates playback. Fields are independent observations, not one transaction. */
typedef enum {
    RADIO_DIAG_SESSION,RADIO_DIAG_ACTIVE,RADIO_DIAG_HTTP_BYTES,RADIO_DIAG_HTTP_READS,
    RADIO_DIAG_HTTP_TIMEOUTS,RADIO_DIAG_MAX_HTTP_US,RADIO_DIAG_QUEUED_BYTES,
    RADIO_DIAG_PCM_FRAMES,RADIO_DIAG_PCM_BYTES,RADIO_DIAG_SAMPLE_RATE,
    RADIO_DIAG_CHANNELS,RADIO_DIAG_REBUFFERS,RADIO_DIAG_MAX_FEED_GAP_US,
    RADIO_DIAG_MAX_DECODE_US,RADIO_DIAG_ERROR,RADIO_DIAG_PCM_QUEUED_BYTES,
    RADIO_DIAG_PCM_UNDERRUNS,RADIO_DIAG_MIN_FREE_HEAP,RADIO_DIAG_WRITER_STACK_FREE,
    RADIO_DIAG_PROBE_MODE,RADIO_DIAG_READ_BYTES,RADIO_DIAG_PROBE_DRAINED,
    RADIO_DIAG_WAITING_FOR_SPACE,RADIO_DIAG_MAX_SPACE_WAIT_US,
    RADIO_DIAG_TIMEOUT_SOCKET_READY,RADIO_DIAG_PEER_FAMILY,RADIO_DIAG_PEER_IP4,
    RADIO_DIAG_TLS,RADIO_DIAG_SOCKET_READY_CHECKS,RADIO_DIAG_COUNT
} radio_diag_metric_t;
typedef enum {
    RADIO_DIAG_OK,RADIO_DIAG_HTTP_OPEN_ERROR,RADIO_DIAG_HTTP_READ_ERROR,
    RADIO_DIAG_HTTP_IDLE_ERROR,RADIO_DIAG_MEMORY_ERROR,RADIO_DIAG_DECODE_ERROR,
    RADIO_DIAG_FORMAT_ERROR,RADIO_DIAG_OUTPUT_ERROR,RADIO_DIAG_INPUT_STALL,
    RADIO_DIAG_START_TIMEOUT
} radio_diag_error_t;
typedef struct {uint32_t values[RADIO_DIAG_COUNT];} radio_diag_snapshot_t;
void radio_diagnostics_begin(void);
void radio_diagnostics_set(radio_diag_metric_t field,uint32_t value);
void radio_diagnostics_add(radio_diag_metric_t field,uint32_t value);
void radio_diagnostics_max(radio_diag_metric_t field,uint32_t value);
/* Minimum fields use UINT32_MAX as the unobserved sentinel after begin. */
void radio_diagnostics_min(radio_diag_metric_t field,uint32_t value);
void radio_diagnostics_get(radio_diag_snapshot_t *out);
/* Implemented by the UI status owner; no LVGL access or allocation. */
unsigned radio_diagnostics_playback_state(void);
#ifdef __cplusplus
}
#endif
