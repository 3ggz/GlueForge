#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace gf::ui
{
    /**
        A TooltipWindow whose hover help can be switched off by the "?" button.
        Scoped to the editor (pass the editor as parent) so multiple plug-in
        instances don't fight over a single global tooltip window.
    */
    class HelpTooltipWindow : public juce::TooltipWindow
    {
    public:
        explicit HelpTooltipWindow (juce::Component* parent)
            : juce::TooltipWindow (parent, 550) {}

        void setHelpEnabled (bool e) { helpEnabled = e; }
        bool isHelpEnabled() const   { return helpEnabled; }

        juce::String getTipFor (juce::Component& c) override
        {
            return helpEnabled ? juce::TooltipWindow::getTipFor (c) : juce::String();
        }

    private:
        bool helpEnabled = true;
    };
}
