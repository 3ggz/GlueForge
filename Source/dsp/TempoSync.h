#pragma once

#include <juce_core/juce_core.h>

namespace gf::dsp
{
    /** Note-division choices shared by the tempo-duck rate and synced-release. */
    inline juce::StringArray divisionChoices()
    {
        return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/4 D", "1/8 D", "1/4 T", "1/8 T" };
    }

    /** Length of a note division in beats (quarter-notes). */
    inline double divisionBeats (int index)
    {
        static const double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 1.5, 0.75, 2.0 / 3.0, 1.0 / 3.0 };
        const int n = (int) (sizeof (beats) / sizeof (beats[0]));
        return beats[juce::jlimit (0, n - 1, index)];
    }

    inline double beatsToMs (double beats, double bpm)
    {
        return beats * 60000.0 / juce::jmax (1.0e-6, bpm);
    }
}
