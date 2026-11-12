#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A track's sections, bar by bar, from how much kick and bass each bar has:
// the drops are where the low end is near its loudest, the breakdowns are the
// stretches without it between drops, the intro before the first drop, the
// outro after the last. Boundaries land on bar lines, snapped to 4-bar
// phrases when one is that close.
struct Section {
    double startSec = 0.0;
    double endSec = 0.0;
    bool drop = false; // the full low end (a drop); false: intro, breakdown or outro
};

struct StructureResult {
    bool ok = false;
    std::vector<Section> sections; // in order, covering the bars from the first downbeat
    double firstDropSec = -1.0;    // where the intro ends: the first drop of 16 bars or more (< 0: no drop)
    std::string bars;              // one letter per bar, for debugging: D drop, . otherwise
};

StructureResult estimateStructure(const std::vector<float> &mono, uint32_t sampleRate, double bpm, double firstBeatSec);
