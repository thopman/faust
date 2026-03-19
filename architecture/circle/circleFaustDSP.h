/************************************************************************
  Circle Faust Architecture File
  Copyright (c) 2025 Thomas Hopman All rights reserved.
 ---------------------------------------------------------------------
 This Architecture section is free software; you can redistribute it
 and/or modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2 of
 the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; If not, see <http://www.gnu.org/licenses/>.

 EXCEPTION : As a special exception, you may create a larger work
 that contains this FAUST architecture section and distribute
 that work under terms of your choice, so long as this FAUST
 architecture section is not modified.
 ************************************************************************/

#ifndef __circle_faust_dsp__
#define __circle_faust_dsp__

//==========================================

#define CIRCLE_SAMPLERATE AUDIO_SAMPLE_RATE

// Forward declarations
class FaustPolyEngine;
class MidiUI;
class circleAudio;
class mydsp;

#ifdef OSCCTRL
class OSCUI_circle;
class CNetSubSystem;
class CTimer;
#endif

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

class circleFaustDSP
{

    private:

        // the polyphonic engine
        FaustPolyEngine* fPolyEngine;

        // the audio driver
        circleAudio* fAudioDriver;

        #ifdef OSCCTRL
        // OSC UI (optional)
        OSCUI_circle* fOSCUI;
        #endif

    public:

        //--------------`circleFaustDSP()`----------------
        // Default constructor, the audio driver will set
        // the sampleRate and buffer size
        //----
        circleFaustDSP(int sampleRate, int bufferSize, int numInputs, int numOutputs);

        // destructor
        ~circleFaustDSP();

        // setup the the hardware buffer pointers.
        void setDSP_ChannelBuffers(FAUSTFLOAT *AudioChannelA_0_Left, FAUSTFLOAT *AudioChannelA_0_Right,
                                    FAUSTFLOAT *AudioChannelB_0_Left, FAUSTFLOAT *AudioChannelB_0_Right);

        //-----------------`void processAudioCallback()`--------------------------
        // Callback to render a buffer.
        //--------------------------------------------------------
        void processAudioCallback();

        //-------`void propagateMidi(int count, double time, int type, int channel, int data1, int data2)`--------
        // Take a raw MIDI message and propagate it to the Faust
        // DSP object. This method can be used concurrently with
        // [`keyOn`](#keyOn) and [`keyOff`](#keyOff).
        //
        // `propagateMidi` can
        // only be used if the `[style:poly]` metadata is used in
        // the Faust code or if `-nvoices` flag has been
        // provided before compilation.
        //
        // #### Arguments
        //
        // * `count`: size of the message (1-3)
        // * `time`: time stamp
        // * `type`: message type (byte)
        // * `channel`: channel number
        // * `data1`: first data byte (should be `null` if `count<2`)
        // * `data2`: second data byte (should be `null` if `count<3`)
        //--------------------------------------------------------
        void propagateMidi(int, double, int, int, int, int);

        #ifdef OSCCTRL
        //-------`void setOSCNetwork(CNetSubSystem* net, int inputPort, int outputPort, int errorPort)`--------
        // Initialize OSC support with Circle network subsystem.
        // Creates 3 UDP sockets for standard OSC communication:
        //   - inputPort (default 5510): receives parameter updates
        //   - outputPort (default 5511): sends bargraph values
        //   - errorPort (default 5512): sends error messages
        //
        // Must be called before processOSC(). Network initialization
        // may fail if ports are already in use.
        //
        // #### Arguments
        //
        // * `net`: Pointer to Circle CNetSubSystem (must be initialized)
        // * `inputPort`: UDP port for receiving OSC messages (default: 5510)
        // * `outputPort`: UDP port for sending bargraphs (default: 5511)
        // * `errorPort`: UDP port for error messages (default: 5512)
        //--------------------------------------------------------
        void setOSCNetwork(CNetSubSystem* net,
                          int inputPort = 5510,
                          int outputPort = 5511,
                          int errorPort = 5512);

        //-------`void setOSCTimer(CTimer* timer)`--------
        // Set timer for bargraph rate limiting (Phase 2).
        // Required for bargraph output functionality.
        //
        // #### Arguments
        //
        // * `timer`: Pointer to Circle CTimer
        //--------------------------------------------------------
        void setOSCTimer(CTimer* timer);

        //-------`bool processOSC()`--------
        // Process incoming OSC messages (non-blocking).
        // Call this regularly from main loop, rate-limited to ~10ms intervals.
        // Never call from audio callback.
        //
        // Handles:
        // - Parameter updates (floats, ints, doubles)
        // - OSC bundles
        // - Discovery messages (/get, /hello)
        // - Bargraph transmission (rate-limited)
        //
        // #### Returns
        //
        // true if a packet was received and processed (busy), false if idle.
        // Use to drive Yield() — only yield when idle to avoid stacking
        // network stack work on top of active I/O (important on single-core).
        //--------------------------------------------------------
        bool processOSC();

        //-------`int getOSCParamsCount()`--------
        // Get total number of OSC-controllable parameters.
        //
        // #### Returns
        //
        // Number of parameters (sliders, buttons, etc.)
        //--------------------------------------------------------
        int getOSCParamsCount();

        //-------`const char* getOSCParamAddress(int index)`--------
        // Get OSC address for parameter at given index.
        //
        // #### Arguments
        //
        // * `index`: Parameter index (0 to getOSCParamsCount()-1)
        //
        // #### Returns
        //
        // OSC address string (e.g., "/faust/synth/volume") or nullptr
        //--------------------------------------------------------
        const char* getOSCParamAddress(int index);
        #endif // OSCCTRL
};

#endif
