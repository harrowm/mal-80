#pragma once
#include <cstdint>
#include <vector>
#include <SDL.h>

// 1-bit audio output emulation for the TRS-80 Model I.
//
// The TRS-80 has no speaker.  Games produce sound by rapidly toggling bit 1
// of port 0xFF (the cassette data line) at audio frequencies.  The cassette
// output jack is connected to an external amplifier.
//
// The original hardware has an RC low-pass filter on the output that smooths
// sharp square-wave edges and prevents pops/clicks when the bit changes state.
// We replicate this with a first-order IIR filter (one multiply + one add
// per sample) using an α chosen to give a ~4 kHz cutoff at 44100 Hz.
//
// SDL_QueueAudio (push model) is used: the emulator accumulates samples
// during step_frame(), then flushes the buffer once per video frame.

class Sound {
public:
    static constexpr int      SAMPLE_RATE      = 44100;
    // T-states per audio sample: 1,774,000 Hz / 44,100 Hz ≈ 40
    static constexpr uint64_t TICKS_PER_SAMPLE = 40;
    // IIR α for ~4 kHz cutoff at 44100 Hz:
    //   RC = 1/(2π × 4000) ≈ 39.8 µs,  dt = 1/44100 ≈ 22.7 µs
    //   α = dt / (RC + dt) ≈ 0.363
    // Smaller α → stronger smoothing / lower cutoff.
    // Larger α → less smoothing / higher cutoff (rawer sound, more pops).
    static constexpr float    LP_ALPHA         = 0.363f;
    // DC-blocking high-pass filter (simulates AC coupling in original hardware).
    // Removes steady-state DC bias so silence is always near zero amplitude,
    // preventing pops when sound starts/stops.
    //   fc ≈ sample_rate × (1 – HP_ALPHA) / 2π ≈ 7 Hz  (passes all audio)
    static constexpr float    HP_ALPHA         = 0.999f;
    // Output amplitude (int16_t peak).  Half of max to leave headroom.
    static constexpr int16_t  AMPLITUDE        = 16384;

    // Open SDL audio device.  Non-fatal: if it fails, update/flush are no-ops.
    bool init();
    void cleanup();

    // Call once per CPU instruction from Emulator::step_frame.
    //   sound_bit : current state of bit 1 of port 0xFF
    //   ticks     : T-states this instruction took
    //   active    : false during cassette I/O or turbo mode (mutes output)
    void update(bool sound_bit, int ticks, bool active);

    // Call once per video frame (NORMAL speed only) to push samples to SDL.
    // Caps the SDL queue at MAX_QUEUED_FRAMES to bound latency.
    void flush();

    // Discard all buffered samples and clear the SDL queue.
    // Call when exiting turbo mode so stale silence doesn't play before
    // game audio.
    void clear();

    static constexpr int MAX_QUEUED_FRAMES = 4;  // max ~67 ms of audio queued

private:
    SDL_AudioDeviceID device_     = 0;
    float             lp_state_   = 0.0f;  // LP filter state (–1.0 … +1.0)
    float             hp_state_   = 0.0f;  // DC-blocking HP filter state
    uint64_t          ticks_acc_  = 0;     // Sub-sample tick accumulator
    std::vector<int16_t> buf_;             // Samples for the current frame
};
