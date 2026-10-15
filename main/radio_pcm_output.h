#pragma once
#include <cstddef>
#include <cstdint>
struct RadioPcmOutput;
struct RadioPcmOutputConfig {
    uint32_t sample_rate;
    bool (*current)(void *);
    uint8_t (*volume)(void *);
    void (*buffering)(bool,void *);
    void *context;
};
/* Producer-side API. Open/close and writes may block and require one owner.
 * Only the sink task touches codec format, volume and PCM while it is alive.
 * The context/callbacks must remain valid until close joins the sink task. */
RadioPcmOutput *radio_pcm_output_open(const RadioPcmOutputConfig &config);
bool radio_pcm_output_write(RadioPcmOutput *output,const void *pcm,std::size_t bytes);
bool radio_pcm_output_played(const RadioPcmOutput *output);
bool radio_pcm_output_failed(const RadioPcmOutput *output);
/* Drain only on natural completion. Cancellation discards queued audio. The
 * join finishes before buffers/callback context can be released. Returns played. */
bool radio_pcm_output_close(RadioPcmOutput *output,bool drain);
