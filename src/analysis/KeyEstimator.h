#pragma once
#include <cstdint>
#include <vector>

// Built-in key estimator, made for electronic music:
//  - a log-frequency spectrogram (thirds of a semitone, C1 .. C8) from a long
//    STFT, so bass notes a semitone apart come out separate;
//  - the track's tuning, from where in each semitone the energy sits (edits
//    resampled to a new tempo aren't at A = 440 any more);
//  - log compression and spectral whitening, so drums and noise (broadband)
//    drop out and the tones stay;
//  - bass (E1 .. Eb3) and treble (C3 .. B6) chroma, each frame counting equally;
//  - scored against key profiles learned from ~700 labelled tracks (bass and
//    treble each, major and minor, plus a bias towards minor).
//
// On those tracks (5-fold cross-validated): 88% exact on keys in the
// filenames, 74% on keys in files' tags; Essentia's EDM profiles got 72% / 59%
// on the same files. Most of the remaining misses agree with Essentia against
// the label (pitched edits, mashups), so edits stay a click away.
struct KeyResult {
    bool ok = false;
    int key = -1;             // 0..23, see core/Key.h
    double confidence = 0.0;  // 0..1, the model's probability for that key
};

KeyResult estimateKey(const std::vector<float> &mono, uint32_t sampleRate);
