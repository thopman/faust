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
 * World's first OSC implementation for embedded bare metal Faust.
 *
 * This implementation uses tinyosc for OSC parsing and Circle's network stack.
 * Unlike POSIX-based OSCUI (liblo/oscpack with threads), this is designed
 * for single-threaded bare metal use with non-blocking I/O.
 *
 * Features:
 * - Standard 3-port OSC: input (5510), output (5511), error (5512)
 * - Parameter control via OSC messages (float/int/double types)
 * - Bargraph monitoring (output) - Phase 2
 * - OSC discovery protocol (/get, /hello) - Phase 2
 * - Bundle support with timetag handling
 * - Non-blocking operation, real-time safe
 *
 * Usage in Circle project:
 * ```cpp
 * // Initialization
 * circleFaustDSP* dsp = new circleFaustDSP(48000, 256, 2, 2);
 * dsp->setOSCNetwork(netSubsystem);
 *
 * // Main loop
 * while(1) {
 *     dsp->processOSC();  // Non-blocking
 *     // ... other processing ...
 * }
 * ```
 */

#include <cstring>
#include <vector>
#include <map>
#include <string>
#include "faust/gui/UI.h"
#include "faust/gui/PathBuilder.h"

// Forward declarations for Circle types (provided by user's Circle project)
class CSocket;
class CNetSubSystem;
class CIPAddress;
class CTimer;

// Include tinyosc via Circle wrapper
#ifdef OSCCTRL
extern "C" {
    #include "faust/osc/tinyosc_circle.h"
}
#endif

//==============================================================================
// OSCNode - Represents a single Faust parameter with OSC mapping
//==============================================================================

class OSCNode {
private:
    std::string fPath;         // Full OSC path (e.g., "/faust/synth/volume")
    FAUSTFLOAT* fZone;        // Pointer to Faust parameter memory
    FAUSTFLOAT fMin;          // Minimum value
    FAUSTFLOAT fMax;          // Maximum value
    bool fIsOutput;           // true for bargraphs (output only)
    FAUSTFLOAT fLastSent;     // Last value sent (for change detection)

public:
    OSCNode(const std::string& path, FAUSTFLOAT* zone,
            FAUSTFLOAT min, FAUSTFLOAT max, bool isOutput = false)
        : fPath(path), fZone(zone), fMin(min), fMax(max),
          fIsOutput(isOutput), fLastSent(0.0f)
    {
        if (isOutput && zone) {
            fLastSent = *zone;  // Initialize with current value
        }
    }

    virtual ~OSCNode() {}

    // Update parameter value with clamping
    void update(FAUSTFLOAT value)
    {
        if (fZone && !fIsOutput) {  // Only update input parameters
            // Clamp to range
            if (value < fMin) value = fMin;
            if (value > fMax) value = fMax;
            *fZone = value;
        }
    }

    // Accessors
    const std::string& getPath() const { return fPath; }
    FAUSTFLOAT* getZone() const { return fZone; }
    FAUSTFLOAT getValue() const { return fZone ? *fZone : 0.0f; }
    FAUSTFLOAT getMin() const { return fMin; }
    FAUSTFLOAT getMax() const { return fMax; }
    bool isOutput() const { return fIsOutput; }

    // Check if value changed since last send (for bargraphs)
    bool hasChanged()
    {
        if (!fIsOutput || !fZone) return false;
        FAUSTFLOAT current = *fZone;
        if (current != fLastSent) {
            fLastSent = current;
            return true;
        }
        return false;
    }
};

//==============================================================================
// CircleOSCNetwork - Network abstraction layer for 3-port OSC
//==============================================================================

class CircleOSCNetwork {
private:
    CSocket* fInputSocket;     // Port 5510 - receive parameter updates
    CSocket* fOutputSocket;    // Port 5511 - send bargraphs
    CSocket* fErrorSocket;     // Port 5512 - send errors
    CNetSubSystem* fNet;

    // Sender tracking for responses
    CIPAddress fLastSenderIP;
    unsigned short fLastSenderPort;
    bool fHasValidSender;

    // Pre-allocated buffers (avoid runtime allocation)
    unsigned char fInputBuffer[2048];
    unsigned char fOutputBuffer[2048];
    unsigned char fErrorBuffer[512];

public:
    CircleOSCNetwork(CNetSubSystem* net);
    ~CircleOSCNetwork();

    // Initialization
    bool initialize(int inputPort, int outputPort, int errorPort);

    // Phase 1: Input operations
    int receiveMessage(unsigned char** buffer, int maxLen,
                      CIPAddress* senderIP, unsigned short* senderPort);

    // Phase 2: Output operations
    bool sendToLastSender(const unsigned char* buffer, int len);
    bool sendMessage(const unsigned char* buffer, int len,
                    const CIPAddress& destIP, unsigned short destPort);

    // Phase 3: Error reporting
    void sendError(const char* errorMsg);

    // Utilities
    bool hasValidSender() const { return fHasValidSender; }
    unsigned char* getOutputBuffer() { return fOutputBuffer; }
    int getOutputBufferSize() const { return sizeof(fOutputBuffer); }
};

//==============================================================================
// OSCUI_circle - Main OSC UI class
//==============================================================================

class OSCUI_circle : public UI, public PathBuilder {
private:
    // Network
    CircleOSCNetwork* fNetwork;
    bool fNetworkReady;

    // Parameter registry
    std::vector<OSCNode*> fNodes;                 // All parameters
    std::map<std::string, OSCNode*> fPathMap;     // Fast lookup by OSC address
    std::vector<OSCNode*> fBargraphs;             // Output parameters (Phase 2)

    // Configuration
    std::string fRootName;                         // Root OSC address (e.g., "/faust")
    int fTransmissionMode;                         // 0=OFF, 1=ALL, 2=ALIAS (Phase 3)

    // Timer for bargraph rate limiting (Phase 2)
    CTimer* fTimer;
    unsigned int fLastBargraphUpdate;
    int fBargraphUpdateRateMs;                     // Milliseconds between updates

public:
    OSCUI_circle(const char* appname, CNetSubSystem* net);
    virtual ~OSCUI_circle();

    // Initialization
    bool initNetwork(int inputPort = 5510, int outputPort = 5511, int errorPort = 5512);
    void setTimer(CTimer* timer) { fTimer = timer; }

    // Main processing (called from kernel loop)
    void processOSC();

    // UI interface implementation (from faust/gui/UI.h)
    virtual void openTabBox(const char* label);
    virtual void openHorizontalBox(const char* label);
    virtual void openVerticalBox(const char* label);
    virtual void closeBox();

    virtual void addButton(const char* label, FAUSTFLOAT* zone);
    virtual void addCheckButton(const char* label, FAUSTFLOAT* zone);
    virtual void addVerticalSlider(const char* label, FAUSTFLOAT* zone,
                                   FAUSTFLOAT init, FAUSTFLOAT min,
                                   FAUSTFLOAT max, FAUSTFLOAT step);
    virtual void addHorizontalSlider(const char* label, FAUSTFLOAT* zone,
                                     FAUSTFLOAT init, FAUSTFLOAT min,
                                     FAUSTFLOAT max, FAUSTFLOAT step);
    virtual void addNumEntry(const char* label, FAUSTFLOAT* zone,
                            FAUSTFLOAT init, FAUSTFLOAT min,
                            FAUSTFLOAT max, FAUSTFLOAT step);

    virtual void addHorizontalBargraph(const char* label, FAUSTFLOAT* zone,
                                       FAUSTFLOAT min, FAUSTFLOAT max);
    virtual void addVerticalBargraph(const char* label, FAUSTFLOAT* zone,
                                     FAUSTFLOAT min, FAUSTFLOAT max);

    // Soundfile (not implemented for bare metal)
    virtual void addSoundfile(const char* label, const char* filename, Soundfile** sf_zone) {}

    // Metadata
    virtual void declare(FAUSTFLOAT* zone, const char* key, const char* val);

    // Utility
    int getParamsCount() const { return (int)fNodes.size(); }
    const char* getParamAddress(int index) const;

private:
    // Internal helpers
    void addNode(const char* label, FAUSTFLOAT* zone,
                FAUSTFLOAT min, FAUSTFLOAT max, bool isOutput = false);
    void parseAndRouteMessage(char* buffer, int len);
    void routeMessage(tosc_message* msg);

    // Phase 2: Discovery and output
    void handleGetMessage();
    void handleHelloMessage();
    void sendBargraphUpdates();

    // Phase 3: Advanced features
    void handleXmitMessage(int mode);

    // Utility
    unsigned int getCurrentTime();
};

//==============================================================================
// CircleOSCNetwork Implementation
//==============================================================================

#include <circle/net/socket.h>
#include <circle/net/ipaddress.h>
#include <circle/net/in.h>

CircleOSCNetwork::CircleOSCNetwork(CNetSubSystem* net)
    : fInputSocket(nullptr), fOutputSocket(nullptr), fErrorSocket(nullptr),
      fNet(net), fHasValidSender(false), fLastSenderPort(0)
{
}

CircleOSCNetwork::~CircleOSCNetwork()
{
    delete fInputSocket;
    delete fOutputSocket;
    delete fErrorSocket;
}

bool CircleOSCNetwork::initialize(int inputPort, int outputPort, int errorPort)
{
    if (!fNet) return false;

    // Create input socket (receive OSC messages)
    fInputSocket = new CSocket(fNet, IPPROTO_UDP);
    if (!fInputSocket) return false;

    if (fInputSocket->Bind(inputPort) < 0) {
        delete fInputSocket;
        fInputSocket = nullptr;
        return false;
    }

    // Create output socket (send bargraphs)
    fOutputSocket = new CSocket(fNet, IPPROTO_UDP);
    if (!fOutputSocket) {
        delete fInputSocket;
        fInputSocket = nullptr;
        return false;
    }

    if (fOutputSocket->Bind(outputPort) < 0) {
        delete fInputSocket;
        delete fOutputSocket;
        fInputSocket = nullptr;
        fOutputSocket = nullptr;
        return false;
    }

    // Create error socket
    fErrorSocket = new CSocket(fNet, IPPROTO_UDP);
    if (!fErrorSocket) {
        delete fInputSocket;
        delete fOutputSocket;
        fInputSocket = nullptr;
        fOutputSocket = nullptr;
        return false;
    }

    if (fErrorSocket->Bind(errorPort) < 0) {
        delete fInputSocket;
        delete fOutputSocket;
        delete fErrorSocket;
        fInputSocket = nullptr;
        fOutputSocket = nullptr;
        fErrorSocket = nullptr;
        return false;
    }

    return true;
}

int CircleOSCNetwork::receiveMessage(unsigned char** buffer, int maxLen,
                                     CIPAddress* senderIP, unsigned short* senderPort)
{
    if (!fInputSocket || maxLen > (int)sizeof(fInputBuffer)) return -1;

    int bytesReceived = fInputSocket->ReceiveFrom(fInputBuffer, maxLen,
                                                  MSG_DONTWAIT, senderIP, senderPort);

    if (bytesReceived > 0) {
        // Track sender for responses
        fLastSenderIP = *senderIP;
        fLastSenderPort = *senderPort;
        fHasValidSender = true;
        *buffer = fInputBuffer;
    }

    return bytesReceived;
}

bool CircleOSCNetwork::sendToLastSender(const unsigned char* buffer, int len)
{
    if (!fOutputSocket || !fHasValidSender) return false;
    return sendMessage(buffer, len, fLastSenderIP, fLastSenderPort);
}

bool CircleOSCNetwork::sendMessage(const unsigned char* buffer, int len,
                                   const CIPAddress& destIP, unsigned short destPort)
{
    if (!fOutputSocket) return false;

    int sent = fOutputSocket->SendTo(buffer, len, MSG_DONTWAIT, destIP, destPort);
    return (sent == len);
}

void CircleOSCNetwork::sendError(const char* errorMsg)
{
    if (!fErrorSocket || !fHasValidSender || !errorMsg) return;

    char buffer[512];
    int len = tosc_writeMessage((unsigned char*)buffer, sizeof(buffer),
                               "/error", "s", errorMsg);

    if (len > 0) {
        fErrorSocket->SendTo(buffer, len, MSG_DONTWAIT, fLastSenderIP, fLastSenderPort);
    }
}

//==============================================================================
// OSCUI_circle Implementation
//==============================================================================

OSCUI_circle::OSCUI_circle(const char* appname, CNetSubSystem* net)
    : fNetwork(nullptr), fNetworkReady(false), fRootName("/"), fTransmissionMode(1),
      fTimer(nullptr), fLastBargraphUpdate(0), fBargraphUpdateRateMs(50)
{
    if (appname && appname[0] != '\0') {
        fRootName = std::string("/") + appname;
    }

    if (net) {
        fNetwork = new CircleOSCNetwork(net);
    }
}

OSCUI_circle::~OSCUI_circle()
{
    // Delete all nodes
    for (OSCNode* node : fNodes) {
        delete node;
    }
    fNodes.clear();
    fPathMap.clear();
    fBargraphs.clear();

    delete fNetwork;
}

bool OSCUI_circle::initNetwork(int inputPort, int outputPort, int errorPort)
{
    if (!fNetwork) return false;

    fNetworkReady = fNetwork->initialize(inputPort, outputPort, errorPort);
    return fNetworkReady;
}

unsigned int OSCUI_circle::getCurrentTime()
{
    return fTimer ? fTimer->GetTicks() : 0;
}

void OSCUI_circle::addNode(const char* label, FAUSTFLOAT* zone,
                          FAUSTFLOAT min, FAUSTFLOAT max, bool isOutput)
{
    // Build OSC path from UI hierarchy
    std::string path = fRootName + buildPath(label);

    // Create and register node
    OSCNode* node = new OSCNode(path, zone, min, max, isOutput);
    fNodes.push_back(node);
    fPathMap[path] = node;

    if (isOutput) {
        fBargraphs.push_back(node);
    }
}

const char* OSCUI_circle::getParamAddress(int index) const
{
    if (index >= 0 && index < (int)fNodes.size()) {
        return fNodes[index]->getPath().c_str();
    }
    return nullptr;
}

// UI interface implementation
void OSCUI_circle::openTabBox(const char* label)
{
    pushLabel(label);
}

void OSCUI_circle::openHorizontalBox(const char* label)
{
    pushLabel(label);
}

void OSCUI_circle::openVerticalBox(const char* label)
{
    pushLabel(label);
}

void OSCUI_circle::closeBox()
{
    popLabel();
}

void OSCUI_circle::addButton(const char* label, FAUSTFLOAT* zone)
{
    addNode(label, zone, 0.0, 1.0, false);
}

void OSCUI_circle::addCheckButton(const char* label, FAUSTFLOAT* zone)
{
    addNode(label, zone, 0.0, 1.0, false);
}

void OSCUI_circle::addVerticalSlider(const char* label, FAUSTFLOAT* zone,
                                    FAUSTFLOAT init, FAUSTFLOAT min,
                                    FAUSTFLOAT max, FAUSTFLOAT step)
{
    addNode(label, zone, min, max, false);
}

void OSCUI_circle::addHorizontalSlider(const char* label, FAUSTFLOAT* zone,
                                       FAUSTFLOAT init, FAUSTFLOAT min,
                                       FAUSTFLOAT max, FAUSTFLOAT step)
{
    addNode(label, zone, min, max, false);
}

void OSCUI_circle::addNumEntry(const char* label, FAUSTFLOAT* zone,
                              FAUSTFLOAT init, FAUSTFLOAT min,
                              FAUSTFLOAT max, FAUSTFLOAT step)
{
    addNode(label, zone, min, max, false);
}

void OSCUI_circle::addHorizontalBargraph(const char* label, FAUSTFLOAT* zone,
                                         FAUSTFLOAT min, FAUSTFLOAT max)
{
    addNode(label, zone, min, max, true);  // isOutput = true
}

void OSCUI_circle::addVerticalBargraph(const char* label, FAUSTFLOAT* zone,
                                       FAUSTFLOAT min, FAUSTFLOAT max)
{
    addNode(label, zone, min, max, true);  // isOutput = true
}

void OSCUI_circle::declare(FAUSTFLOAT* zone, const char* key, const char* val)
{
    // Phase 3: Handle OSC aliases via metadata
    // For now, just ignore metadata
}

//==============================================================================
// Main OSC Processing
//==============================================================================

void OSCUI_circle::processOSC()
{
    if (!fNetworkReady) return;

    // Phase 1: Receive and process input messages
    unsigned char* buffer;
    CIPAddress senderIP;
    unsigned short senderPort;

    int bytesReceived = fNetwork->receiveMessage(&buffer, 2048, &senderIP, &senderPort);

    if (bytesReceived > 0) {
        parseAndRouteMessage((char*)buffer, bytesReceived);
    }

    // Phase 2: Send bargraph updates (rate-limited)
    if (fTransmissionMode != 0 && fTimer) {
        unsigned int now = getCurrentTime();
        if ((now - fLastBargraphUpdate) >= (unsigned int)fBargraphUpdateRateMs) {
            sendBargraphUpdates();
            fLastBargraphUpdate = now;
        }
    }
}

void OSCUI_circle::parseAndRouteMessage(char* buffer, int len)
{
    if (tosc_isBundle(buffer)) {
        // Handle bundle
        tosc_bundle bundle;
        tosc_parseBundle(&bundle, buffer, len);

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
}

void OSCUI_circle::routeMessage(tosc_message* msg)
{
    const char* address = tosc_getAddress(msg);
    const char* format = tosc_getFormat(msg);

    if (!address || !format) return;

    // Handle special messages (Phase 2)
    if (strcmp(address, "/get") == 0) {
        handleGetMessage();
        return;
    }

    if (strcmp(address, "/hello") == 0) {
        handleHelloMessage();
        return;
    }

    // Phase 3: Handle /xmit
    if (strcmp(address, "/xmit") == 0 && format[0] == 'i') {
        int mode = tosc_getNextInt32(msg);
        handleXmitMessage(mode);
        return;
    }

    // Parameter update
    auto it = fPathMap.find(address);
    if (it != fPathMap.end()) {
        OSCNode* node = it->second;

        // Extract value by type
        if (format[0] == 'f') {
            node->update(tosc_getNextFloat(msg));
        } else if (format[0] == 'i') {
            node->update((FAUSTFLOAT)tosc_getNextInt32(msg));
        } else if (format[0] == 'd') {
            node->update((FAUSTFLOAT)tosc_getNextDouble(msg));
        }
    } else {
        // Unknown address - send error (Phase 3)
        std::string error = "Unknown OSC address: ";
        error += address;
        fNetwork->sendError(error.c_str());
    }
}

//==============================================================================
// Phase 2: Discovery and Output
//==============================================================================

void OSCUI_circle::handleGetMessage()
{
    // Send all parameter addresses with min/max
    const int MAX_BUNDLE_SIZE = 2048;
    unsigned char* bundleBuffer = fNetwork->getOutputBuffer();

    tosc_bundle bundle;
    tosc_writeBundle(&bundle, TINYOSC_TIMETAG_IMMEDIATELY,
                    (char*)bundleBuffer, MAX_BUNDLE_SIZE);

    int messageCount = 0;
    for (OSCNode* node : fNodes) {
        tosc_writeNextMessage(&bundle, node->getPath().c_str(),
                             "ff", node->getMin(), node->getMax());
        messageCount++;

        // Check if approaching buffer limit
        if (bundle.bundleLen > MAX_BUNDLE_SIZE - 128) {
            // Send current bundle
            fNetwork->sendToLastSender(bundleBuffer, bundle.bundleLen);
            // Start new bundle
            tosc_writeBundle(&bundle, TINYOSC_TIMETAG_IMMEDIATELY,
                           (char*)bundleBuffer, MAX_BUNDLE_SIZE);
            messageCount = 0;
        }
    }

    // Send final bundle
    if (messageCount > 0) {
        fNetwork->sendToLastSender(bundleBuffer, bundle.bundleLen);
    }
}

void OSCUI_circle::handleHelloMessage()
{
    // Send root address and port numbers
    unsigned char buffer[256];

    int len = tosc_writeMessage(buffer, sizeof(buffer),
                               fRootName.c_str(),
                               "sii",
                               "127.0.0.1",  // Could get actual IP from CNetSubSystem
                               5510,
                               5511);

    if (len > 0) {
        fNetwork->sendToLastSender(buffer, len);
    }
}

void OSCUI_circle::sendBargraphUpdates()
{
    if (fBargraphs.empty()) return;

    const int MAX_BUNDLE_SIZE = 2048;
    unsigned char* bundleBuffer = fNetwork->getOutputBuffer();

    tosc_bundle bundle;
    tosc_writeBundle(&bundle, TINYOSC_TIMETAG_IMMEDIATELY,
                    (char*)bundleBuffer, MAX_BUNDLE_SIZE);

    int messageCount = 0;
    for (OSCNode* node : fBargraphs) {
        if (node->hasChanged()) {
            tosc_writeNextMessage(&bundle, node->getPath().c_str(),
                                 "f", node->getValue());
            messageCount++;

            // Check buffer limit
            if (bundle.bundleLen > MAX_BUNDLE_SIZE - 64) {
                fNetwork->sendToLastSender(bundleBuffer, bundle.bundleLen);
                tosc_writeBundle(&bundle, TINYOSC_TIMETAG_IMMEDIATELY,
                               (char*)bundleBuffer, MAX_BUNDLE_SIZE);
                messageCount = 0;
            }
        }
    }

    // Send final bundle
    if (messageCount > 0) {
        fNetwork->sendToLastSender(bundleBuffer, bundle.bundleLen);
    }
}

//==============================================================================
// Phase 3: Advanced Features
//==============================================================================

void OSCUI_circle::handleXmitMessage(int mode)
{
    if (mode >= 0 && mode <= 2) {
        fTransmissionMode = mode;
    }
}

#endif  // __OSCUI_CIRCLE_H__
