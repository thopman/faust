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

// Forward declarations for Circle pointer types
class CSocket;
class CNetSubSystem;
class CTimer;

// Full include needed: CIPAddress is a value member (not a pointer)
#include <circle/net/ipaddress.h>

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
    // Single bound socket — Circle requires Bind() before SendTo() works.
    // Input and output both go through this socket (same pattern as network_test).
    CSocket* fSocket;          // Bound to 5510: receives on 5510, sends to remote:5511/5512
    CNetSubSystem* fNet;

    // Destination tracking (set from first received packet, per Faust desthost model)
    CIPAddress fDestHost;
    int fOutputPort;
    int fErrPort;
    bool fHasValidSender;

    // Pre-allocated receive buffer
    unsigned char fInputBuffer[2048];

public:
    CircleOSCNetwork(CNetSubSystem* net);
    ~CircleOSCNetwork();

    // Initialization
    bool initialize(int inputPort, int outputPort, int errorPort);

    // Input
    int receiveMessage(unsigned char** buffer, int maxLen,
                      CIPAddress* senderIP, unsigned short* senderPort);

    // Output (send via single bound socket)
    bool sendToLastSender(const unsigned char* buffer, int len);
    void sendError(const char* errorMsg);

    // Utilities
    bool hasValidSender() const { return fHasValidSender; }
    int getOutputPort() const { return fOutputPort; }
    int getErrPort() const { return fErrPort; }
    void getDeviceIP(char* buf, int len) const;
    // The OSC "desthost": where replies are sent = the last sender (the client),
    // NOT this device's own IP. "0.0.0.0" until a client has been heard.
    void getLastSenderIP(char* buf, int len) const;
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
    std::string fJSONCache;                        // Cached JSONUI output, set via setJSON()

    // Timer for bargraph rate limiting (Phase 2)
    CTimer* fTimer;
    unsigned int fLastBargraphUpdate;
    int fBargraphUpdateRateTicks;                  // Centi-seconds (GetTicks() units, HZ=100, 1 tick=10ms)
    unsigned int fLastClientRxTime;                // GetTicks() of last received client msg (gates keepalive)

    // Key event hook (polyphony): "/keyon i i" and "/keyoff i" call this so
    // notes can be played over OSC without MIDI hardware. Owner (circleFaustDSP)
    // routes it to FaustPolyEngine::keyOn/keyOff; a no-op on non-poly DSPs.
    typedef void (*key_handler_t)(void* arg, bool on, int pitch, int velocity);
    key_handler_t fKeyHandler;
    void* fKeyHandlerArg;

public:
    OSCUI_circle(const char* appname, CNetSubSystem* net);
    virtual ~OSCUI_circle();

    // Initialization
    bool initNetwork(int inputPort = 5510, int outputPort = 5511, int errorPort = 5512);
    void setTimer(CTimer* timer) { fTimer = timer; }
    void setJSON(const std::string& json) { fJSONCache = json; }
    void setKeyHandler(key_handler_t handler, void* arg) { fKeyHandler = handler; fKeyHandlerArg = arg; }

    // Main processing (called from kernel loop)
    bool processOSC();

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
    void handleUIGetMessage();
    void sendBargraphUpdates();
    void sendKeepAlive();

    // Phase 3: Advanced features
    void handleXmitMessage(int mode);

    // Utility
    unsigned int getCurrentTime();
};

//==============================================================================
// CircleOSCNetwork Implementation
//==============================================================================

#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/net/ipaddress.h>
#include <circle/net/in.h>
#include <cstdio>

CircleOSCNetwork::CircleOSCNetwork(CNetSubSystem* net)
    : fSocket(nullptr),
      fNet(net), fOutputPort(5511), fErrPort(5512), fHasValidSender(false)
{
}

CircleOSCNetwork::~CircleOSCNetwork()
{
    delete fSocket;
}

void CircleOSCNetwork::getDeviceIP(char* buf, int len) const
{
    if (fNet) {
        const u8* ip = fNet->GetConfig()->GetIPAddress()->Get();
        snprintf(buf, len, "%u.%u.%u.%u",
                 (unsigned)ip[0], (unsigned)ip[1],
                 (unsigned)ip[2], (unsigned)ip[3]);
    } else {
        snprintf(buf, len, "0.0.0.0");
    }
}

void CircleOSCNetwork::getLastSenderIP(char* buf, int len) const
{
    if (fHasValidSender) {
        const u8* ip = fDestHost.Get();
        snprintf(buf, len, "%u.%u.%u.%u",
                 (unsigned)ip[0], (unsigned)ip[1],
                 (unsigned)ip[2], (unsigned)ip[3]);
    } else {
        snprintf(buf, len, "0.0.0.0");
    }
}

bool CircleOSCNetwork::initialize(int inputPort, int outputPort, int errorPort)
{
    if (!fNet) return false;

    // Single socket: bind to inputPort so ReceiveFrom works AND SendTo works.
    // Circle requires Bind() before SendTo() — unbound sockets return NOT_CONNECTED.
    fSocket = new CSocket(fNet, IPPROTO_UDP);
    if (!fSocket) return false;

    if (fSocket->Bind(inputPort) < 0) {
        delete fSocket;
        fSocket = nullptr;
        return false;
    }

    fOutputPort = outputPort;
    fErrPort = errorPort;
    return true;
}

int CircleOSCNetwork::receiveMessage(unsigned char** buffer, int maxLen,
                                     CIPAddress* senderIP, unsigned short* senderPort)
{
    if (!fSocket || maxLen > (int)sizeof(fInputBuffer)) return -1;

    int bytesReceived = fSocket->ReceiveFrom(fInputBuffer, maxLen,
                                             MSG_DONTWAIT, senderIP, senderPort);

    if (bytesReceived > 0) {
        // First packet sets the destination host (Faust desthost model)
        fDestHost = *senderIP;
        fHasValidSender = true;
        *buffer = fInputBuffer;
    }

    return bytesReceived;
}

bool CircleOSCNetwork::sendToLastSender(const unsigned char* buffer, int len)
{
    if (!fSocket || !fHasValidSender) return false;

    int sent = fSocket->SendTo(buffer, len, MSG_DONTWAIT, fDestHost, fOutputPort);
    return (sent == (int)len);
}

void CircleOSCNetwork::sendError(const char* errorMsg)
{
    if (!fSocket || !fHasValidSender || !errorMsg) return;

    char buffer[512];
    int len = tosc_writeMessage(buffer, sizeof(buffer),
                               "/error", "s", errorMsg);

    if (len > 0) {
        fSocket->SendTo(buffer, len, MSG_DONTWAIT, fDestHost, fErrPort);
    }
}

//==============================================================================
// OSCUI_circle Implementation
//==============================================================================

OSCUI_circle::OSCUI_circle(const char* appname, CNetSubSystem* net)
    : fNetwork(nullptr), fNetworkReady(false), fRootName("/"), fTransmissionMode(1),
      fTimer(nullptr), fLastBargraphUpdate(0), fBargraphUpdateRateTicks(5),  // 5 ticks = 50ms
      fLastClientRxTime(0), fKeyHandler(nullptr), fKeyHandlerArg(nullptr)
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

bool OSCUI_circle::processOSC()
{
    if (!fNetworkReady) return false;

    // Phase 1: Receive and process input messages
    unsigned char* buffer;
    CIPAddress senderIP;
    unsigned short senderPort;

    int bytesReceived = fNetwork->receiveMessage(&buffer, 2048, &senderIP, &senderPort);

    if (bytesReceived > 0) {
        parseAndRouteMessage((char*)buffer, bytesReceived);
    }

    // Periodic outgoing traffic (rate-limited). Runs in EVERY mode: the BCM4343
    // WiFi TX path idles without a steady packet cadence, and lone replies
    // (/get, /hello, /ui) then get dropped below the socket even though SendTo
    // reports success. xmit only gates DATA: bargraphs when on, a tiny keepalive
    // when off — so one-shot replies always reach the wire.
    if (fTimer) {
        unsigned int now = getCurrentTime();

        // Note when a client was last heard, to gate the keepalive below.
        if (bytesReceived > 0) {
            fLastClientRxTime = now;
        }

        if ((now - fLastBargraphUpdate) >= (unsigned int)fBargraphUpdateRateTicks) {
            if (fTransmissionMode != 0) {
                sendBargraphUpdates();
            } else {
                // RX-gate: only keep the WiFi TX warm while a client is actually
                // talking to us (heard within KEEPALIVE_GATE_TICKS). Otherwise we
                // would heartbeat a departed client forever — which bounces back as
                // an ICMP "port unreachable" flood once it closes its socket.
                const unsigned int KEEPALIVE_GATE_TICKS = 1000;  // 10s @ HZ=100 (1 tick=10ms)
                if (fNetwork->hasValidSender() &&
                    (now - fLastClientRxTime) < KEEPALIVE_GATE_TICKS) {
                    sendKeepAlive();
                }
            }
            fLastBargraphUpdate = now;
        }
    }

    return (bytesReceived > 0);
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

    if (strcmp(address, "/ui") == 0 && format[0] == 's') {
        const char* arg = tosc_getNextString(msg);
        if (arg && strcmp(arg, "get") == 0) {
            handleUIGetMessage();
            return;
        }
    }

    // Phase 3: Handle /xmit
    if (strcmp(address, "/xmit") == 0 && format[0] == 'i') {
        int mode = tosc_getNextInt32(msg);
        handleXmitMessage(mode);
        return;
    }

    // Polyphony: "/keyon i i" (pitch, velocity) and "/keyoff i" (pitch) —
    // play notes over OSC, no MIDI hardware needed
    if (strcmp(address, "/keyon") == 0 && format[0] == 'i' && format[1] == 'i') {
        int pitch = tosc_getNextInt32(msg);
        int velocity = tosc_getNextInt32(msg);
        if (fKeyHandler) fKeyHandler(fKeyHandlerArg, true, pitch, velocity);
        return;
    }
    if (strcmp(address, "/keyoff") == 0 && format[0] == 'i') {
        int pitch = tosc_getNextInt32(msg);
        if (fKeyHandler) fKeyHandler(fKeyHandlerArg, false, pitch, 0);
        return;
    }

    // Per-parameter query: /path s get → fff current min max
    if (format[0] == 's') {
        const char* arg = tosc_getNextString(msg);
        if (arg && strcmp(arg, "get") == 0) {
            auto it = fPathMap.find(address);
            if (it != fPathMap.end()) {
                OSCNode* node = it->second;
                unsigned char buf[256];
                int len = tosc_writeMessage((char*)buf, sizeof(buf),
                                           address, "fff",
                                           node->getValue(),
                                           node->getMin(),
                                           node->getMax());
                if (len > 0) fNetwork->sendToLastSender(buf, len);
            }
            return;
        }
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
    // Metadata header lines (Faust OSC spec)
    unsigned char hdr[256];
    char destIP[16];
    // "desthost" = where we send replies = the last sender (the client), not us.
    fNetwork->getLastSenderIP(destIP, sizeof(destIP));

    int len = tosc_writeMessage((char*)hdr, sizeof(hdr),
        fRootName.c_str(), "si", "xmit", fTransmissionMode);
    if (len > 0) fNetwork->sendToLastSender(hdr, len);

    len = tosc_writeMessage((char*)hdr, sizeof(hdr),
        fRootName.c_str(), "ss", "desthost", destIP);
    if (len > 0) fNetwork->sendToLastSender(hdr, len);

    len = tosc_writeMessage((char*)hdr, sizeof(hdr),
        fRootName.c_str(), "si", "outport", fNetwork->getOutputPort());
    if (len > 0) fNetwork->sendToLastSender(hdr, len);

    len = tosc_writeMessage((char*)hdr, sizeof(hdr),
        fRootName.c_str(), "si", "errport", fNetwork->getErrPort());
    if (len > 0) fNetwork->sendToLastSender(hdr, len);

    // Per-parameter: fff current min max
    const int MAX_BUNDLE_SIZE = 2048;
    unsigned char bundleBuffer[MAX_BUNDLE_SIZE];

    tosc_bundle bundle;
    tosc_writeBundle(&bundle, TINYOSC_TIMETAG_IMMEDIATELY,
                    (char*)bundleBuffer, MAX_BUNDLE_SIZE);

    int messageCount = 0;
    for (OSCNode* node : fNodes) {
        tosc_writeNextMessage(&bundle, node->getPath().c_str(),
                             "fff", node->getValue(), node->getMin(), node->getMax());
        messageCount++;

        if (bundle.bundleLen > MAX_BUNDLE_SIZE - 128) {
            fNetwork->sendToLastSender(bundleBuffer, bundle.bundleLen);
            tosc_writeBundle(&bundle, TINYOSC_TIMETAG_IMMEDIATELY,
                           (char*)bundleBuffer, MAX_BUNDLE_SIZE);
            messageCount = 0;
        }
    }

    if (messageCount > 0) {
        fNetwork->sendToLastSender(bundleBuffer, bundle.bundleLen);
    }
}

void OSCUI_circle::handleHelloMessage()
{
    // Send root address and port numbers
    unsigned char buffer[256];

    char ipStr[16];
    fNetwork->getDeviceIP(ipStr, sizeof(ipStr));
    // TODO: wire ports to accessors when auto port-alloc lands (ROADMAP Phase 3).
    // Requires storing fInputPort + getInputPort() so all three ports report the
    // actual bound values, not these literals. Hardcoded is honest-by-default today.
    int len = tosc_writeMessage((char*)buffer, sizeof(buffer),
                               fRootName.c_str(),
                               "siii",
                               ipStr,
                               5510,
                               5511,
                               5512);

    if (len > 0) {
        fNetwork->sendToLastSender(buffer, len);
    }
}

void OSCUI_circle::handleUIGetMessage()
{
    if (fJSONCache.empty()) {
        fNetwork->sendError("UI JSON not available");
        return;
    }
    unsigned char buf[4096];
    int len = tosc_writeMessage((char*)buf, sizeof(buf),
                               "/ui", "s", fJSONCache.c_str());
    if (len > 0) fNetwork->sendToLastSender(buf, len);
}

void OSCUI_circle::sendBargraphUpdates()
{
    if (fBargraphs.empty()) return;

    const int MAX_BUNDLE_SIZE = 2048;
    unsigned char bundleBuffer[MAX_BUNDLE_SIZE];

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

// Tiny heartbeat sent only when xmit==0, to keep the WiFi TX path from idling
// (see processOSC). A real OSC client can use it as a liveness signal or ignore
// the address. Skipped until a sender is known (nothing to keep warm before
// first contact). Path: <root>/heartbeat, one int (always 1).
void OSCUI_circle::sendKeepAlive()
{
    if (!fNetwork->hasValidSender()) return;

    unsigned char buf[64];
    std::string path = fRootName + "/heartbeat";
    int len = tosc_writeMessage((char*)buf, sizeof(buf), path.c_str(), "i", 1);
    if (len > 0) fNetwork->sendToLastSender(buf, len);
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
