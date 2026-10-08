#pragma once

#include <juce_graphics/juce_graphics.h>

// Pastels on paper
namespace Theme
{
    inline const juce::Colour paper{ 0xfff3ecdc };
    inline const juce::Colour ink{ 0xff3d3a4b }; // soft charcoal
    inline const juce::Colour inkSoft{ 0xff7a7486 };
    inline const juce::Colour pink{ 0xfff2a5b5 };
    inline const juce::Colour blue{ 0xff98c1e3 };
    inline const juce::Colour green{ 0xffa6d6b4 };
    inline const juce::Colour yellow{ 0xfff4d67a };
    inline const juce::Colour lavender{ 0xffc3b0e0 };
    inline const juce::Colour peach{ 0xfff5b88a };

    inline juce::Colour slotColour(int slot)
    {
        const juce::Colour colours[] = { pink, blue, green, yellow };
        return colours[(size_t)slot % 4];
    }
}