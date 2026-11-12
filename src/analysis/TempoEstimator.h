#pragma once
#include <cstdint>
#include <vector>

// Built-in tempo + beat-grid estimator for constant-tempo electronic music.
//  - Tempo: spectral-flux onset envelopes (whole band and kick band), their
//    autocorrelation scored for repetition at 1, 2, 4, 8 and 16 beats (a real
//    beat repeats in bars and phrases; DnB's two-step heard at 2/3 doesn't),
//    86-181 BPM, then whole-track folding to ~0.01 BPM, snapped to whole / .5.
//  - Beats: the folded kick pulse on a 1.5 ms envelope, placed where it rises
//    (not its steepest point: real kicks take a few ms to build).
//  - Downbeat: where the music starts, when it starts on the grid (edits);
//    else a small model of each beat's band energies and changes per place
//    in the bar (learned on edits cut at off-beat points); else the first
//    strong hit.
// Measured on my music: 211/227 filename BPMs exact; cut at off-beat
// points, 609/666 grids on the beat (15 ms) and 541/666 with the right 1.
//
// This is the stage-1 analyzer. Stage 2 replaces it with Beat This (neural
// beat + downbeat tracking); the result shape stays the same.
struct TempoResult {
    bool ok = false;
    double bpm = 0.0;
    double firstBeatSec = 0.0; // a downbeat
    double confidence = 0.0;   // 0..1, rough
};

TempoResult estimateTempo(const std::vector<float> &mono, uint32_t sampleRate,
                          double minBpm = 86.0, double maxBpm = 181.0);
