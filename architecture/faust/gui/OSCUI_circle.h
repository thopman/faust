/************************************************************************
 FAUST Architecture File
 Copyright (C) 2025 Thomas Hopman, Tomatek Audio
 Copyright (C) 2003-2022 GRAME, Centre National de Creation Musicale
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
 ************************************************************************/

#ifndef __OSCUI_CIRCLE_H__
#define __OSCUI_CIRCLE_H__

/*
 * OSCUI_circle - Bare Metal OSC UI for Raspberry Pi Circle
 *
 * This is a minimal OSC implementation for bare metal embedded systems.
 * It uses tinyosc for OSC parsing and Circle's network stack.
 *
 * Unlike POSIX-based OSCUI which uses liblo/oscpack with threads,
 * this implementation is designed for single-threaded bare metal use.
 *
 * Usage in Circle project:
 * - Call processOSC() regularly from main loop
 * - Provides non-blocking OSC parameter updates
 */

#include <cstring>
#include <vector>
#include <map>
#include "faust/gui/UI.h"
#include "faust/gui/PathBuilder.h"

// Forward declarations for Circle network types
// These will be provided by the user's Circle project
class CSocket;
class CNetSubSystem;
class CIPAddress;

// Include tinyosc via Circle wrapper (user must provide this)
// Example: lib/tinyosc_circle.h that includes tinyosc.h
#ifdef OSCCTRL
extern "C" {
    #include "tinyosc_circle.h"
}
#endif

//==============================================================================
// OSC Address Utility
//==============================================================================

class OSCAddress {
public:
    // Convert UI label to valid OSC address
    // Replaces spaces with '_' and invalid chars with '-'
    static std::string sanitize(const std::string& label) {
        std::string result = label;
        for (size_t i = 0; i < result.length(); i++) {
            char c = result[i];
            // OSC spec: must not contain: space # * , / ? [ ] { }
            if (c == ' ' || c == '\t') {
                result[i] = '_';
            } else if (c == '#' || c == '*' || c == ',' || c == '/' ||
                       c == '?' || c == '[' || c == ']' || c == '{' || c == '}') {
                result[i] = '-';
            }
        }
        return result;
    }
};

//==============================================================================
// OSC Node - represents a single parameter
//==============================================================================

class OSCNode {
private:
    std::string fPath;      // Full OSC path (e.g., "/synth/volume")
    FAUSTFLOAT* fZone;      // Pointer to Faust parameter
    FAUSTFLOAT fMin;        // Minimum value
    FAUSTFLOAT fMax;        // Maximum value

public:
    OSCNode(const std::string& path, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max)
        : fPath(path), fZone(zone), fMin(min), fMax(max) {}

    const std::string& getPath() const { return fPath; }

    // Update parameter from OSC value
    void update(FAUSTFLOAT value) {
        if (fZone) {
            // Clamp value to min/max range
            if (value < fMin) value = fMin;
            if (value > fMax) value = fMax;
            *fZone = value;
        }
    }

    FAUSTFLOAT getValue() const { return fZone ? *fZone : 0.0f; }
    FAUSTFLOAT getMin() const { return fMin; }
    FAUSTFLOAT getMax() const { return fMax; }
};

//==============================================================================
// OSCUI_circle - Main OSC UI class for Circle (bare metal)
//==============================================================================

class OSCUI_circle : public UI, public PathBuilder {
private:
    std::vector<OSCNode*> fNodes;                    // All OSC-controllable parameters
    std::map<std::string, OSCNode*> fPathMap;       // Fast lookup by OSC path

    // Network components (provided by user's Circle kernel)
    CSocket* fSocket;
    CNetSubSystem* fNet;

    // Buffer for receiving OSC messages
    static const int OSC_BUFFER_SIZE = 2048;
    unsigned char fOscBuffer[OSC_BUFFER_SIZE];

    // Root name for OSC addresses
    std::string fRootName;

    // Flag to track if network is initialized
    bool fNetworkReady;

public:
    OSCUI_circle(const char* appname, CSocket* socket, CNetSubSystem* net)
        : fSocket(socket)
        , fNet(net)
        , fRootName(appname ? std::string("/") + appname : "/faust")
        , fNetworkReady(socket != nullptr && net != nullptr)
    {
    }

    virtual ~OSCUI_circle() {
        // Delete all nodes
        for (size_t i = 0; i < fNodes.size(); i++) {
            delete fNodes[i];
        }
        fNodes.clear();
        fPathMap.clear();
    }

    //--------------------------------------------------------------------------
    // Process incoming OSC messages (call this regularly from main loop)
    //--------------------------------------------------------------------------
    void processOSC() {
        if (!fNetworkReady || !fSocket) return;

#ifdef OSCCTRL
        CIPAddress foreignIP;
        unsigned short foreignPort;

        // Non-blocking receive
        int bytesReceived = fSocket->ReceiveFrom(
            fOscBuffer,
            OSC_BUFFER_SIZE,
            MSG_DONTWAIT,  // Circle's non-blocking flag
            &foreignIP,
            &foreignPort
        );

        if (bytesReceived > 0) {
            parseOSCMessage((char*)fOscBuffer, bytesReceived);
        }
#endif
    }

    //--------------------------------------------------------------------------
    // Parse and route OSC message to parameters
    //--------------------------------------------------------------------------
    void parseOSCMessage(char* buffer, int len) {
#ifdef OSCCTRL
        // Check if it's a bundle or single message
        if (tosc_isBundle(buffer)) {
            tosc_bundle bundle;
            tosc_parseBundle(&bundle, buffer, len);

            // Process all messages in bundle
            tosc_message msg;
            while (tosc_getNextMessage(&bundle, &msg)) {
                routeMessage(&msg);
            }
        } else {
            // Single message
            tosc_message msg;
            if (tosc_parseMessage(&msg, buffer, len) == 0) {
                routeMessage(&msg);
            }
        }
#endif
    }

    //--------------------------------------------------------------------------
    // Route parsed OSC message to the right parameter
    //--------------------------------------------------------------------------
    void routeMessage(tosc_message* msg) {
#ifdef OSCCTRL
        const char* address = tosc_getAddress(msg);
        const char* format = tosc_getFormat(msg);

        if (!address || !format) return;

        // Look up the parameter by OSC path
        std::map<std::string, OSCNode*>::iterator it = fPathMap.find(address);
        if (it != fPathMap.end()) {
            OSCNode* node = it->second;

            // Extract value based on type tag
            if (format[0] == 'f') {
                // Float
                float value = tosc_getNextFloat(msg);
                node->update(value);
            } else if (format[0] == 'i') {
                // Integer (convert to float)
                int32_t value = tosc_getNextInt32(msg);
                node->update((FAUSTFLOAT)value);
            } else if (format[0] == 'd') {
                // Double (convert to float)
                double value = tosc_getNextDouble(msg);
                node->update((FAUSTFLOAT)value);
            }
            // Add more type handlers as needed
        }
#endif
    }

    //--------------------------------------------------------------------------
    // UI Interface Implementation
    //--------------------------------------------------------------------------

    // -- widget's layouts
    virtual void openTabBox(const char* label) { pushLabel(label); }
    virtual void openHorizontalBox(const char* label) { pushLabel(label); }
    virtual void openVerticalBox(const char* label) { pushLabel(label); }
    virtual void closeBox() { popLabel(); }

    // -- active widgets
    virtual void addButton(const char* label, FAUSTFLOAT* zone) {
        addNode(label, zone, 0.0f, 1.0f);
    }

    virtual void addCheckButton(const char* label, FAUSTFLOAT* zone) {
        addNode(label, zone, 0.0f, 1.0f);
    }

    virtual void addVerticalSlider(const char* label, FAUSTFLOAT* zone,
                                   FAUSTFLOAT init, FAUSTFLOAT min,
                                   FAUSTFLOAT max, FAUSTFLOAT step) {
        addNode(label, zone, min, max);
    }

    virtual void addHorizontalSlider(const char* label, FAUSTFLOAT* zone,
                                     FAUSTFLOAT init, FAUSTFLOAT min,
                                     FAUSTFLOAT max, FAUSTFLOAT step) {
        addNode(label, zone, min, max);
    }

    virtual void addNumEntry(const char* label, FAUSTFLOAT* zone,
                            FAUSTFLOAT init, FAUSTFLOAT min,
                            FAUSTFLOAT max, FAUSTFLOAT step) {
        addNode(label, zone, min, max);
    }

    // -- passive widgets (bargraphs are read-only, no OSC input)
    virtual void addHorizontalBargraph(const char* label, FAUSTFLOAT* zone,
                                      FAUSTFLOAT min, FAUSTFLOAT max) {
        // Could implement OSC output for monitoring
    }

    virtual void addVerticalBargraph(const char* label, FAUSTFLOAT* zone,
                                    FAUSTFLOAT min, FAUSTFLOAT max) {
        // Could implement OSC output for monitoring
    }

    // -- metadata
    virtual void declare(FAUSTFLOAT* zone, const char* key, const char* val) {
        // Could handle OSC-specific metadata here
    }

    //--------------------------------------------------------------------------
    // Utility: Get OSC address info
    //--------------------------------------------------------------------------
    int getParamsCount() const { return (int)fNodes.size(); }

    const char* getParamAddress(int index) const {
        if (index >= 0 && index < (int)fNodes.size()) {
            return fNodes[index]->getPath().c_str();
        }
        return nullptr;
    }

    void printAddresses() const {
        // For debugging - can be called to print all OSC addresses
        for (size_t i = 0; i < fNodes.size(); i++) {
            // In Circle, use CLogger or serial output
            // printf("%s [%.2f - %.2f]\n",
            //        fNodes[i]->getPath().c_str(),
            //        fNodes[i]->getMin(),
            //        fNodes[i]->getMax());
        }
    }

private:
    //--------------------------------------------------------------------------
    // Add a new OSC node for a parameter
    //--------------------------------------------------------------------------
    void addNode(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT min, FAUSTFLOAT max) {
        // Build full path: /rootname/group1/group2/.../parameter
        std::string path = fRootName + buildPath(label);
        path = "/" + OSCAddress::sanitize(path.substr(1)); // Remove and re-add leading /

        // Create node
        OSCNode* node = new OSCNode(path, zone, min, max);
        fNodes.push_back(node);
        fPathMap[path] = node;
    }
};

#endif // __OSCUI_CIRCLE_H__
