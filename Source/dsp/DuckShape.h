#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace gf::dsp
{
    struct ShapeNode { float phase = 0.0f; float value = 0.0f; }; // value: 1 = open, 0 = fully ducked

    /**
        Editable pump/duck shape (LFOTool-style). Up to 16 nodes over one cycle
        (phase 0..1) with fixed endpoints at 0 and 1; compiled to a 256-point
        piecewise-linear lookup table for O(1), allocation-free evaluation on the
        audio thread. Serializes to/from a compact "p:v p:v …" string for state
        recall. fromString() always sanitizes (clamp, sort, endpoints, cap).
    */
    class DuckShape
    {
    public:
        static constexpr int kMaxNodes = 16;
        static constexpr int kLutSize  = 256;

        DuckShape() { setDefault(); }

        void setDefault()
        {
            nodes_[0] = { 0.0f, 0.0f };   // ducked on the downbeat
            nodes_[1] = { 0.5f, 0.6f };
            nodes_[2] = { 1.0f, 1.0f };   // recovered by the next beat
            count_ = 3;
            rebuild();
        }

        void setNodes (const ShapeNode* in, int n)
        {
            count_ = juce::jlimit (2, kMaxNodes, n);
            for (int i = 0; i < count_; ++i)
            {
                nodes_[(size_t) i].phase = juce::jlimit (0.0f, 1.0f, in[i].phase);
                nodes_[(size_t) i].value = juce::jlimit (0.0f, 1.0f, in[i].value);
            }
            sanitize();
            rebuild();
        }

        int       getNumNodes() const   { return count_; }
        ShapeNode getNode (int i) const  { return nodes_[(size_t) juce::jlimit (0, count_ - 1, i)]; }

        // O(1) linear-interpolated lookup; phase wrapped to [0,1).
        float valueAt (float phase) const
        {
            const float p    = phase - std::floor (phase);
            const float fpos = p * (float) (kLutSize - 1);
            const int   i0   = juce::jlimit (0, kLutSize - 1, (int) fpos);
            const int   i1   = juce::jmin (i0 + 1, kLutSize - 1);
            const float frac = fpos - (float) i0;
            return lut_[(size_t) i0] + frac * (lut_[(size_t) i1] - lut_[(size_t) i0]);
        }

        const float* lut() const { return lut_.data(); }

        juce::String toString() const
        {
            juce::String s;
            for (int i = 0; i < count_; ++i)
            {
                if (i > 0) s << ' ';
                s << juce::String (nodes_[(size_t) i].phase, 4) << ':' << juce::String (nodes_[(size_t) i].value, 4);
            }
            return s;
        }

        void fromString (const juce::String& str)
        {
            const auto toks = juce::StringArray::fromTokens (str, " ", "");
            int n = 0;
            for (auto& t : toks)
            {
                if (n >= kMaxNodes) break;
                const auto pv = juce::StringArray::fromTokens (t, ":", "");
                if (pv.size() == 2)
                {
                    nodes_[(size_t) n].phase = juce::jlimit (0.0f, 1.0f, (float) pv[0].getDoubleValue());
                    nodes_[(size_t) n].value = juce::jlimit (0.0f, 1.0f, (float) pv[1].getDoubleValue());
                    ++n;
                }
            }
            if (n < 2) { setDefault(); return; }
            count_ = n;
            sanitize();
            rebuild();
        }

    private:
        void sanitize()
        {
            std::sort (nodes_.begin(), nodes_.begin() + count_,
                       [] (const ShapeNode& a, const ShapeNode& b) { return a.phase < b.phase; });
            nodes_[0].phase             = 0.0f; // force endpoints
            nodes_[(size_t) (count_ - 1)].phase = 1.0f;
        }

        void rebuild()
        {
            int seg = 0;
            for (int i = 0; i < kLutSize; ++i)
            {
                const float p = (float) i / (float) (kLutSize - 1);
                while (seg < count_ - 2 && p > nodes_[(size_t) (seg + 1)].phase) ++seg;
                const auto& a = nodes_[(size_t) seg];
                const auto& b = nodes_[(size_t) (seg + 1)];
                const float denom = b.phase - a.phase;
                const float t = denom > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (p - a.phase) / denom) : 0.0f;
                lut_[(size_t) i] = a.value + t * (b.value - a.value);
            }
        }

        std::array<ShapeNode, kMaxNodes> nodes_;
        int count_ = 3;
        std::array<float, kLutSize> lut_ {};
    };
}
