/************************** BEGIN circleAudio.h *****************************
 FAUST Architecture File
 Copyright (C) 2003-2025 GRAME, Centre National de Creation Musicale
 ---------------------------------------------------------------------
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation; either version 2.1 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 
 EXCEPTION : As a special exception, you may create a larger work
 that contains this FAUST architecture section and distribute
 that work under terms of your choice, so long as this FAUST
 architecture section is not modified.
 ***********************************************************************/

#ifndef __circle_audio__
#define __circle_audio__

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <iomanip>
			
#include "faust/dsp/dsp.h"
#include "faust/audio/audio.h"

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif

class circleAudio : public audio {

    private:

        ::dsp* fDSP;

        int iSampleRate;
        int iBufferSize;
        int iNumInputs;
        int iNumOutputs;
    
        // Faust convention for input/output arrays
        FAUSTFLOAT *inputsArray[8];
        FAUSTFLOAT *outputsArray[8];

    public:
    
        circleAudio(int sampleRate, int bufferSize, int numInputs, int numOutputs)
        {
            iSampleRate = sampleRate;
            iBufferSize = bufferSize;
            iNumInputs = numInputs;
            iNumOutputs = numOutputs;
        }
    
        virtual ~circleAudio() 
        {
            // nothing for now
        }
    
        // the pi hardware DSP supports up to 2 channels (1 stereo pair).
        virtual void setDSP_ChannelBuffers(FAUSTFLOAT *AudioChannelA_0_Left, FAUSTFLOAT *AudioChannelA_0_Right, 
                                            FAUSTFLOAT *AudioChannelB_0_Left, FAUSTFLOAT *AudioChannelB_0_Right)
        {
            // set the pointers, generalized for the pi's 2 channels.
            inputsArray[0] = AudioChannelB_0_Left;
            inputsArray[1] = AudioChannelB_0_Right;

            
            outputsArray[0] = AudioChannelA_0_Left;
            outputsArray[1] = AudioChannelA_0_Right;

        }
    
        virtual bool init(const char* name, ::dsp* dsp)
        {
            fDSP = dsp;                // this should be fFinalDSP
            fDSP->init(iSampleRate);   // this sets the sample rate
            return true;
        }

        virtual bool start()
        {
            // Nothing for now. Will want to find the pi way to start.
            return true;
        }

        virtual void stop()
        {
            // nothing for now. Will want to find the pi way to stop.
        }
    
        void processAudioCallback()
        {
            // Faust compute function
            fDSP->compute(iBufferSize, inputsArray, outputsArray);
        }
         
        virtual int getBufferSize() { return iBufferSize; }
        virtual int getSampleRate() { return iSampleRate; }
        virtual int getNumInputs()  { return iNumInputs;  }
        virtual int getNumOutputs() { return iNumOutputs; }    
};
					
#endif
/**************************  END  circleAudio.h **************************/
