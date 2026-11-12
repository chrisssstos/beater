#pragma once
#include "core/BeatGrid.h"
#include "core/TrackBuffer.h"

#include <atomic>
#include <memory>
#include <vector>

namespace RubberBand {
class RubberBandStretcher;
}

enum class PhaseMode : int { Beat = 0, Bar = 1 };

// Master clock as seen by a deck for one render block.
struct MasterState {
    double bpm = 125.0;
    double beat = 0.0; // master beat position at the first frame of the block
    double sampleRate = 48000.0;
    PhaseMode phase = PhaseMode::Beat;
};

struct DeckCommand {
    enum Type : int {
        None,
        Load,           // track = new holder (engine takes ownership), a = cue frame
        Play,           // start now (phase aligned if sync is on)
        Pause,
        Seek,           // a = frame (phase aligned if playing + sync)
        SetGrid,        // a = bpm, b = firstBeatSec
        SetLoop,        // a = startBeat, b = endBeat (b <= a disables region)
        SetLoopEnabled, // a != 0
        SetSync,        // a != 0
        PlayAt,         // start when the master reaches beat a, at track beat b then (to the sample's block)
    };
    Type type = None;
    double a = 0.0;
    double b = 0.0;
    TrackBufferPtr *track = nullptr;
};

// One deck, run on the audio thread. Owns a real-time Rubber Band stretcher
// (R3 engine) so tempo and pitch are independent.
class DeckEngine {
public:
    explicit DeckEngine(uint32_t sampleRate);
    ~DeckEngine();
    DeckEngine(const DeckEngine &) = delete;
    DeckEngine &operator=(const DeckEngine &) = delete;

    // ---- audio thread ----
    // Returns a holder that must be freed on a non-audio thread (or nullptr).
    TrackBufferPtr *handle(const DeckCommand &cmd, const MasterState &m);
    // Mixes n frames (n <= kMaxBlock) into outL/outR.
    void render(float *outL, float *outR, int n, const MasterState &m);

    static constexpr int kMaxBlock = 1024;

    // ---- parameters written by the UI thread ----
    std::atomic<float> volume{1.0f};
    std::atomic<int> semitones{0};

    // ---- state published by the audio thread ----
    std::atomic<double> pubPosFrame{0.0};
    std::atomic<bool> pubPlaying{false};
    std::atomic<float> pubLevel{0.0f};
    std::atomic<double> pubPhaseError{0.0}; // beats, + = ahead of master
    std::atomic<double> pubSpeed{1.0};      // playback speed relative to the original

private:
    enum class Pending { None, Seek, Pause };

    double targetBaseRatio(const MasterState &m) const;
    double alignedFrame(double frame, const MasterState &m) const;
    void startAt(double frame, double ratio, double pitch);
    void feed(float *l, float *r, int count);
    void recomputeLoopFrames();
    bool loopValid() const { return m_loopEnabled && m_loopEndF > m_loopStartF + 1.0; }
    void publish();

    uint32_t m_sr;
    std::unique_ptr<RubberBand::RubberBandStretcher> m_rb;

    TrackBufferPtr *m_track = nullptr;
    BeatGrid m_grid;
    bool m_sync = true;
    bool m_loopEnabled = true;
    double m_loopStartBeat = 0.0, m_loopEndBeat = 0.0;
    double m_loopStartF = 0.0, m_loopEndF = 0.0;

    bool m_playing = false;
    bool m_scheduled = false; // PlayAt waiting for its master beat
    double m_schedMasterBeat = 0.0, m_schedTrackBeat = 0.0;
    Pending m_pending = Pending::None;
    double m_pendingFrame = 0.0;
    bool m_fadeIn = false;
    int m_fadePos = 0;

    double m_feedPos = 0.0;    // next input frame for the stretcher
    bool m_feedInLoop = false;
    double m_audiblePos = 0.0; // input frame currently heard at the output
    bool m_audibleInLoop = false;
    size_t m_discard = 0;
    double m_ratio = 1.0;
    double m_pitch = 1.0;
    float m_lastGain = 0.0f;
    float m_level = 0.0f;
    double m_phaseErr = 0.0;

    std::vector<float> m_inL, m_inR, m_outL, m_outR;
};
