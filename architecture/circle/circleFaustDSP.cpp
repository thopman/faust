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

#include <math.h>
#include <cmath>

#include "faust/misc.h"
#include "faust/gui/UI.h"
#include "faust/dsp/dsp.h"
#include "faust/dsp/dsp-adapter.h"
#include "faust/gui/meta.h"

//**************************************************************
// Intrinsic
//**************************************************************

<<includeIntrinsic>>

<<includeclass>>

//**************************************************************
// Polyphony
//**************************************************************

#include "faust/dsp/faust-poly-engine.h"

//**************************************************************
// Audio driver
//**************************************************************

#include "faust/audio/circleAudio.h"

//**************************************************************
// OSC Support (optional)
//**************************************************************

#ifdef OSCCTRL
#include "faust/gui/OSCUI_circle.h"
#endif

//**************************************************************
// Interface
//**************************************************************

#include "circleFaustDSP.h"

std::list<GUI*> GUI::fGuiList;
ztimedmap GUI::gTimedZoneMap;

// constructor
circleFaustDSP::circleFaustDSP(int sampleRate, int bufferSize, int numInputs, int numOutputs)
{
    // create a new instance of the audio driver.
    fAudioDriver = new circleAudio(sampleRate, bufferSize, numInputs, numOutputs);

    // create a new instance of the FaustPolyEngine, the constructor calls DSP init
    fPolyEngine = new FaustPolyEngine(new mydsp(), fAudioDriver);

    #ifdef OSCCTRL
    // OSC UI will be initialized later via setOSCNetwork()
    fOSCUI = nullptr;
    #endif
}

// destructor
circleFaustDSP::~circleFaustDSP()
{
    #ifdef OSCCTRL
    delete fOSCUI;
    #endif

    // DSP and fAudioDriver are kept and deleted by fPolyEngine
    delete fPolyEngine;
}

// setup the sampleRate and bufferSize
// void setDSP_Parameters(int sampleRate, int bufferSize);
void circleFaustDSP::setDSP_ChannelBuffers(FAUSTFLOAT *AudioChannelA_0_Left, FAUSTFLOAT *AudioChannelA_0_Right,
                                            FAUSTFLOAT *AudioChannelB_0_Left, FAUSTFLOAT *AudioChannelB_0_Right)
{
    fAudioDriver->setDSP_ChannelBuffers(AudioChannelA_0_Left, AudioChannelA_0_Right, AudioChannelB_0_Left, AudioChannelB_0_Right);
}

void circleFaustDSP::processAudioCallback()
{
    // ask the driver to process the audio callback
    fAudioDriver->processAudioCallback();
}

void circleFaustDSP::propagateMidi(int count, double time, int type, int channel, int data1, int data2)
{
    fPolyEngine->propagateMidi(count, time, type, channel, data1, data2);
}

//**************************************************************
// OSC Methods
//**************************************************************

#ifdef OSCCTRL

void circleFaustDSP::setOSCNetwork(CSocket* socket, CNetSubSystem* net)
{
    if (!fOSCUI && socket && net) {
        // Create OSCUI instance with network components
        fOSCUI = new OSCUI_circle("faust", socket, net);

        // Build the UI - this registers all parameters with OSCUI
        fPolyEngine->buildUserInterface(fOSCUI);
    }
}

void circleFaustDSP::processOSC()
{
    if (fOSCUI) {
        fOSCUI->processOSC();
    }
}

int circleFaustDSP::getOSCParamsCount()
{
    if (fOSCUI) {
        return fOSCUI->getParamsCount();
    }
    return 0;
}

const char* circleFaustDSP::getOSCParamAddress(int index)
{
    if (fOSCUI) {
        return fOSCUI->getParamAddress(index);
    }
    return nullptr;
}

#endif // OSCCTRL
