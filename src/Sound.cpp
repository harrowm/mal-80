#include "Sound.hpp"
#include <iostream>

bool Sound::init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::cerr << "[SOUND] SDL_InitSubSystem(AUDIO) failed: "
                  << SDL_GetError() << "\n";
        return false;
    }

    SDL_AudioSpec want{}, have{};
    want.freq     = SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 512;       // internal buffer size (≈11.6 ms)
    want.callback = nullptr;   // queue mode: we push via SDL_QueueAudio

    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device_ == 0) {
        std::cerr << "[SOUND] SDL_OpenAudioDevice failed: "
                  << SDL_GetError() << "\n";
        return false;         // non-fatal — emulator continues without sound
    }

    // Reserve ~1 frame worth of samples so push is allocation-free
    buf_.reserve(SAMPLE_RATE / 60 + 64);

    SDL_PauseAudioDevice(device_, 0);  // start playback
    std::cout << "[SOUND] Audio opened: " << have.freq << " Hz, "
              << (int)have.channels << " ch, "
              << "buffer " << have.samples << " samples\n";
    return true;
}

void Sound::cleanup() {
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void Sound::update(bool sound_bit, int ticks, bool active) {
    if (device_ == 0) return;

    // Bipolar input: bit high → +1.0, bit low → –1.0.
    // When muted (cassette active or turbo mode), drive toward 0 so the
    // filter decays smoothly to silence without a hard pop.
    float raw = active ? (sound_bit ? 1.0f : -1.0f) : 0.0f;

    ticks_acc_ += static_cast<uint64_t>(ticks);
    while (ticks_acc_ >= TICKS_PER_SAMPLE) {
        ticks_acc_ -= TICKS_PER_SAMPLE;

        // First-order IIR low-pass filter (RC circuit simulation):
        //   y[n] = α × x[n] + (1–α) × y[n–1]
        float lp = LP_ALPHA * raw + (1.0f - LP_ALPHA) * lp_state_;

        // DC-blocking high-pass filter (simulates AC coupling):
        //   hp[n] = lp[n] – lp[n–1] + HP_ALPHA × hp[n–1]
        // Removes steady DC bias so silence stays near zero.
        float hp = lp - lp_state_ + HP_ALPHA * hp_state_;

        lp_state_ = lp;
        hp_state_ = hp;

        buf_.push_back(static_cast<int16_t>(hp * AMPLITUDE));
    }
}

void Sound::flush() {
    if (device_ == 0) { buf_.clear(); return; }
    if (buf_.empty()) return;

    // Partial-push cap: compute how many samples we can add before hitting the
    // MAX_QUEUED_FRAMES ceiling.  Push only that leading slice; silently drop
    // the tail.  This absorbs frame-rate jitter without hard audio gaps.
    const uint32_t max_bytes    = static_cast<uint32_t>(
        (SAMPLE_RATE / 60) * MAX_QUEUED_FRAMES * sizeof(int16_t));
    const uint32_t queued_bytes = SDL_GetQueuedAudioSize(device_);
    if (queued_bytes < max_bytes) {
        uint32_t room_bytes    = max_bytes - queued_bytes;
        uint32_t push_bytes    = static_cast<uint32_t>(
            buf_.size() * sizeof(int16_t));
        if (push_bytes > room_bytes) push_bytes = room_bytes;
        SDL_QueueAudio(device_, buf_.data(), push_bytes);
    }
    buf_.clear();
}

void Sound::clear() {
    buf_.clear();
    lp_state_ = 0.0f;  // reset filters so first real sample doesn't pop
    hp_state_ = 0.0f;
    ticks_acc_ = 0;
    if (device_) SDL_ClearQueuedAudio(device_);
}
