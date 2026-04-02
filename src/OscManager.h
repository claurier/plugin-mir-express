#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
 * OscManager
 *
 * Thin wrapper around juce::OSCSender.  Owned by PluginEditor; shared by
 * OscSetupOverlay (configuration) and DescriptorDisplay (sending).
 */
class OscManager
{
public:
    OscManager() = default;

    //==========================================================================
    /** Apply new settings.  Re-connects if enabled; disconnects if not. */
    void configure (const juce::String& ip, int port, bool enabled)
    {
        sender.disconnect();
        oscEnabled = enabled;
        currentIp   = ip;
        currentPort = port;

        if (oscEnabled)
        {
            const bool ok = sender.connect (ip, port);
            if (! ok)
                oscEnabled = false;   // connection failed — treat as off
        }
    }

    /** Send a single float value.  No-op when disabled. */
    void sendFloat (const juce::OSCAddressPattern& address, float value)
    {
        if (! oscEnabled) return;
        sender.send (address, value);
    }

    bool        isEnabled()    const noexcept { return oscEnabled;    }
    juce::String getIp()       const noexcept { return currentIp;     }
    int          getPort()     const noexcept { return currentPort;   }

private:
    juce::OSCSender sender;
    bool            oscEnabled  { false };
    juce::String    currentIp   { "127.0.0.1" };
    int             currentPort { 9000 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscManager)
};
