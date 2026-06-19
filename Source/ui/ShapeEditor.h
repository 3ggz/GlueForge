#pragma once

#include "LookAndFeel.h"
#include "../PluginProcessor.h"
#include "../dsp/DuckShape.h"

#include <vector>

namespace gf::ui
{
    /**
        LFOTool/VolumeShaper-style pump-shape editor. Draggable nodes (drag to
        move, double-click to add on the curve or remove a node), a live playhead
        showing where the host is in the cycle. The curve+grid are rendered once to
        a cached image and re-rendered only on edit/resize; the 30 Hz tick just
        blits that image and moves the playhead — cheap.

        The shape round-trips through the processor (APVTS state tree), so it
        recalls with the session, A/B and presets.
    */
    class ShapeEditor : public juce::Component
    {
    public:
        explicit ShapeEditor (GlueForgeProcessor& p) : proc (p) { fetchNodes(); }

        // Called from the editor's timer: resync if the shape changed underneath
        // us (session load / A/B / preset), then move the playhead.
        void tick()
        {
            if (proc.getShapeGeneration() != localGen_) { fetchNodes(); rebuildImage(); }
            phase_ = proc.getDuckPhase();
            repaint();
        }

        void resized() override { rebuildImage(); }

        void paint (juce::Graphics& g) override
        {
            if (curveImage_.isValid()) g.drawImageAt (curveImage_, 0, 0);
            const auto b = area();
            const float x = b.getX() + juce::jlimit (0.0f, 1.0f, phase_) * b.getWidth();
            g.setColour (colours::accent2.withAlpha (0.85f));
            g.drawLine (x, b.getY(), x, b.getBottom(), 1.5f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragIndex_ = hitTest (e.position.toFloat());
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragIndex_ < 0) return;
            const auto nn = xyToNode (e.position.toFloat());
            auto& nd = nodes_[(size_t) dragIndex_];
            nd.value = nn.value;
            const bool endpoint = (dragIndex_ == 0 || dragIndex_ == (int) nodes_.size() - 1);
            if (! endpoint)
            {
                const float lo = nodes_[(size_t) dragIndex_ - 1].phase + 0.002f;
                const float hi = nodes_[(size_t) dragIndex_ + 1].phase - 0.002f;
                nd.phase = juce::jlimit (lo, hi, nn.phase);
            }
            pushToProcessor();   // live: hear the change while dragging
            rebuildImage();
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            dragIndex_ = -1;
            fetchNodes();        // pick up the sanitized result
            rebuildImage();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int hit = hitTest (e.position.toFloat());
            if (hit > 0 && hit < (int) nodes_.size() - 1)        // remove an interior node
                nodes_.erase (nodes_.begin() + hit);
            else if (hit < 0 && (int) nodes_.size() < gf::dsp::DuckShape::kMaxNodes)
                nodes_.push_back (xyToNode (e.position.toFloat())); // add a node
            else
                return;

            pushToProcessor();
            fetchNodes();
            rebuildImage();
            repaint();
        }

    private:
        static constexpr float pad_ = 6.0f;

        juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (pad_); }

        juce::Point<float> nodeToXY (const gf::dsp::ShapeNode& n) const
        {
            const auto b = area();
            return { b.getX() + n.phase * b.getWidth(), b.getY() + (1.0f - n.value) * b.getHeight() };
        }

        gf::dsp::ShapeNode xyToNode (juce::Point<float> p) const
        {
            const auto b = area();
            return { juce::jlimit (0.0f, 1.0f, (p.x - b.getX()) / juce::jmax (1.0f, b.getWidth())),
                     juce::jlimit (0.0f, 1.0f, 1.0f - (p.y - b.getY()) / juce::jmax (1.0f, b.getHeight())) };
        }

        int hitTest (juce::Point<float> p) const
        {
            for (int i = 0; i < (int) nodes_.size(); ++i)
                if (nodeToXY (nodes_[(size_t) i]).getDistanceFrom (p) <= 9.0f)
                    return i;
            return -1;
        }

        void fetchNodes()
        {
            gf::dsp::DuckShape s; s.fromString (proc.getDuckShapeString());
            nodes_.clear();
            for (int i = 0; i < s.getNumNodes(); ++i) nodes_.push_back (s.getNode (i));
            localGen_ = proc.getShapeGeneration();
        }

        void pushToProcessor()
        {
            gf::dsp::DuckShape s; s.setNodes (nodes_.data(), (int) nodes_.size());
            proc.setDuckShapeString (s.toString());
            localGen_ = proc.getShapeGeneration(); // our own change must not trigger a re-fetch
        }

        void rebuildImage()
        {
            const int w = getWidth(), h = getHeight();
            if (w <= 0 || h <= 0) return;
            curveImage_ = juce::Image (juce::Image::ARGB, w, h, true);
            juce::Graphics g (curveImage_);
            const auto b = area();

            g.setColour (colours::panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

            g.setColour (colours::track.withAlpha (0.5f));
            for (int i = 0; i <= 4; ++i)
            {
                const float x = b.getX() + (float) i / 4.0f * b.getWidth();
                const float y = b.getY() + (float) i / 4.0f * b.getHeight();
                g.drawVerticalLine   ((int) x, b.getY(), b.getBottom());
                g.drawHorizontalLine ((int) y, b.getX(), b.getRight());
            }

            juce::Path curve;
            for (size_t i = 0; i < nodes_.size(); ++i)
            {
                const auto pt = nodeToXY (nodes_[i]);
                if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
            }
            juce::Path fill = curve;
            fill.lineTo (b.getRight(), b.getBottom());
            fill.lineTo (b.getX(), b.getBottom());
            fill.closeSubPath();
            g.setColour (colours::accent.withAlpha (0.15f));
            g.fillPath (fill);
            g.setColour (colours::accent);
            g.strokePath (curve, juce::PathStrokeType (2.0f));

            for (auto& n : nodes_)
            {
                const auto pt = nodeToXY (n);
                g.setColour (colours::bg);   g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (pt));
                g.setColour (colours::text); g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre (pt));
            }
        }

        GlueForgeProcessor& proc;
        std::vector<gf::dsp::ShapeNode> nodes_;
        juce::Image curveImage_;
        int   localGen_  = -1;
        int   dragIndex_ = -1;
        float phase_     = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeEditor)
    };
}
