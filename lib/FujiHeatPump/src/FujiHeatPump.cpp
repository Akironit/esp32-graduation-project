#include "FujiHeatPump.h"



FujiFrame FujiHeatPump::decodeFrame() {
    FujiFrame ff;

    ff.messageSource       =  readBuf[0];
    ff.messageDest         =  readBuf[1] & 0b01111111;
    ff.messageType         = (readBuf[2] & 0b00110000) >> 4;

    ff.acError             = (readBuf[kErrorIndex] & kErrorMask) >> kErrorOffset;
    ff.temperature         = (readBuf[kTemperatureIndex] & kTemperatureMask) >> kTemperatureOffset;
    ff.acMode              = (readBuf[kModeIndex] & kModeMask) >> kModeOffset;
    ff.fanMode             = (readBuf[kFanIndex] & kFanMask) >> kFanOffset;
    ff.economyMode         = (readBuf[kEconomyIndex] & kEconomyMask) >> kEconomyOffset;
    ff.swingMode           = (readBuf[kSwingIndex] & kSwingMask) >> kSwingOffset;
    ff.swingStep           = (readBuf[kSwingStepIndex] & kSwingStepMask) >> kSwingStepOffset;
    ff.controllerPresent   = (readBuf[kControllerPresentIndex] & kControllerPresentMask) >> kControllerPresentOffset;
    ff.updateMagic         = (readBuf[kUpdateMagicIndex] & kUpdateMagicMask) >> kUpdateMagicOffset;
    ff.onOff               = (readBuf[kEnabledIndex] & kEnabledMask) >> kEnabledOffset;
    ff.controllerTemp      = (readBuf[kControllerTempIndex] & kControllerTempMask) >> kControllerTempOffset; // there is one leading bit here that is unknown - probably a sign bit for negative temps?

    ff.writeBit =   (readBuf[2] & 0b00001000) != 0;
    ff.loginBit =   (readBuf[1] & 0b00100000) != 0;
    ff.unknownBit = (readBuf[1] & 0b10000000)  > 0;

    return ff;
}

void FujiHeatPump::encodeFrame(FujiFrame ff){

    memset(writeBuf, 0, 8);

    writeBuf[0] = ff.messageSource;

    writeBuf[1] &= 0b10000000;
    writeBuf[1] |= ff.messageDest & 0b01111111;

    writeBuf[2] &= 0b11001111;
    writeBuf[2] |= ff.messageType << 4;

    if(ff.writeBit){
        writeBuf[2] |= 0b00001000;
    } else {
        writeBuf[2] &= 0b11110111;
    }

    writeBuf[1] &= 0b01111111;
    if(ff.unknownBit) {
        writeBuf[1] |= 0b10000000;
    }

    if(ff.loginBit){
        writeBuf[1] |= 0b00100000;
    } else {
        writeBuf[1] &= 0b11011111;
    }

    writeBuf[kModeIndex] =              (writeBuf[kModeIndex]              & ~kModeMask)              | (ff.acMode << kModeOffset);
    writeBuf[kModeIndex] =              (writeBuf[kEnabledIndex]           & ~kEnabledMask)           | (ff.onOff << kEnabledOffset);
    writeBuf[kFanIndex] =               (writeBuf[kFanIndex]               & ~kFanMask)               | (ff.fanMode << kFanOffset);
    writeBuf[kErrorIndex] =             (writeBuf[kErrorIndex]             & ~kErrorMask)             | (ff.acError << kErrorOffset);
    writeBuf[kEconomyIndex] =           (writeBuf[kEconomyIndex]           & ~kEconomyMask)           | (ff.economyMode << kEconomyOffset);
    writeBuf[kTemperatureIndex] =       (writeBuf[kTemperatureIndex]       & ~kTemperatureMask)       | (ff.temperature << kTemperatureOffset);
    writeBuf[kSwingIndex] =             (writeBuf[kSwingIndex]             & ~kSwingMask)             | (ff.swingMode << kSwingOffset);
    writeBuf[kSwingStepIndex] =         (writeBuf[kSwingStepIndex]         & ~kSwingStepMask)         | (ff.swingStep << kSwingStepOffset);
    writeBuf[kControllerPresentIndex] = (writeBuf[kControllerPresentIndex] & ~kControllerPresentMask) | (ff.controllerPresent << kControllerPresentOffset);
    writeBuf[kUpdateMagicIndex] =       (writeBuf[kUpdateMagicIndex]       & ~kUpdateMagicMask)       | (ff.updateMagic << kUpdateMagicOffset);
    writeBuf[kControllerTempIndex] =    (writeBuf[kControllerTempIndex]    & ~kControllerTempMask)    | (ff.controllerTemp << kControllerTempOffset);

}

void FujiHeatPump::applyControllerTempOverride(FujiFrame& ff) {
    if (!controllerTempOverrideEnabled) {
        return;
    }

    ff.controllerTemp = controllerTempOverride;
    currentState.controllerTemp = controllerTempOverride;
}

void FujiHeatPump::connect(HardwareSerial *serial, bool secondary){
    return this->connect(serial, secondary, -1, -1);
}

void FujiHeatPump::connect(HardwareSerial *serial, bool secondary, int rxPin=-1, int txPin=-1){
    _serial = serial;
    if(rxPin != -1 && txPin != -1) {
#ifdef ESP32
        _serial->begin(500, SERIAL_8E1, rxPin, txPin);
#else
        Serial.print("Setting RX/TX pin unsupported, using defaults.\n");
        _serial->begin(500, SERIAL_8E1);
#endif
    } else {
        _serial->begin(500, SERIAL_8E1);
    }
    _serial->setTimeout(200);
    
    if(secondary) {
        controllerIsPrimary = false;
        controllerAddress = static_cast<byte>(FujiAddress::SECONDARY);
    } else {
        controllerIsPrimary = true;
        controllerAddress = static_cast<byte>(FujiAddress::PRIMARY);
    }
    
    lastFrameReceived = 0;
    updateFields = 0;
    pendingFrame = false;
    seenPrimaryController = false;
    seenSecondaryController = false;
}

Print& FujiHeatPump::getDebugOutput() {
    return debugOutput != nullptr ? *debugOutput : Serial;
}

void FujiHeatPump::printFrame(Print& output, byte buf[8], FujiFrame ff) {
  output.printf("%X %X %X %X %X %X %X %X  ", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
  output.printf(" mSrc: %d mDst: %d mType: %d write: %d login: %d unknown: %d onOff: %d temp: %d, mode: %d cP:%d uM:%d cTemp:%d acError:%d \n", 
    ff.messageSource, ff.messageDest, ff.messageType, ff.writeBit, ff.loginBit, ff.unknownBit, ff.onOff, ff.temperature, ff.acMode, ff.controllerPresent, ff.updateMagic, ff.controllerTemp, ff.acError);

}

void FujiHeatPump::sendPendingFrame() {
    if(pendingFrame && (millis() - lastFrameReceived) > 50) {
        _serial->write(writeBuf, 8);
        _serial->flush();
        pendingFrame = false;
        updateFields = 0;
        frameSyncCount = 0;

        _serial->readBytes(writeBuf, 8); // read back our own frame so we dont process it again
    }
}

bool FujiHeatPump::readNextFrame() {
    while(_serial->available()) {
        frameSyncBuf[frameSyncCount] = ((byte)_serial->read()) ^ 0xFF;

        if(frameSyncCount < 7) {
            frameSyncCount++;
            continue;
        }

        memcpy(readBuf, frameSyncBuf, 8);

        if(isPlausibleFrame(readBuf)) {
            frameSyncCount = 0;
            return true;
        }

        memmove(frameSyncBuf, frameSyncBuf + 1, 7);
        frameSyncCount = 7;
    }

    return false;
}

bool FujiHeatPump::isKnownAddress(byte address) {
    return address == static_cast<byte>(FujiAddress::START)
        || address == static_cast<byte>(FujiAddress::UNIT)
        || address == static_cast<byte>(FujiAddress::PRIMARY)
        || address == static_cast<byte>(FujiAddress::SECONDARY);
}

bool FujiHeatPump::isPlausibleFrame(byte buf[8]) {
    const byte source = buf[0];
    const byte dest = buf[1] & 0b01111111;
    const byte messageType = (buf[2] & 0b00110000) >> 4;

    if(!isKnownAddress(source) || !isKnownAddress(dest)) {
        return false;
    }

    if(source == static_cast<byte>(FujiAddress::START) && dest == static_cast<byte>(FujiAddress::START)) {
        return false;
    }

    if(source == dest) {
        return false;
    }

    return messageType <= static_cast<byte>(FujiMessageType::UNKNOWN);
}

bool FujiHeatPump::waitForFrame() {
    FujiFrame ff;
    
    if(readNextFrame()) {
    
        ff = decodeFrame();

        if(ff.messageSource == static_cast<byte>(FujiAddress::PRIMARY)) {
            seenPrimaryController = true;
        } else if(ff.messageSource == static_cast<byte>(FujiAddress::SECONDARY)) {
            seenSecondaryController = true;
        }

        if(debugPrint) {
            Print& output = getDebugOutput();
            output.print("--> ");
            printFrame(output, readBuf, ff);
        }
        
        if(ff.messageDest == controllerAddress) {
            lastFrameReceived = millis();
            
            if(ff.messageType == static_cast<byte>(FujiMessageType::STATUS)){

                if(ff.controllerPresent == 1) {
                    // we have logged into the indoor unit
                    // this is what most frames are
                    ff.messageSource     = controllerAddress;
                    
                    if(seenSecondaryController) {
                        ff.messageDest       = static_cast<byte>(FujiAddress::SECONDARY);
                        ff.loginBit          = true;
                        ff.controllerPresent = 0;
                    } else {
                        ff.messageDest       = static_cast<byte>(FujiAddress::UNIT);
                        ff.loginBit          = false;
                        ff.controllerPresent = 1;
                    }
                    
                    ff.updateMagic       = 0;
                    ff.unknownBit        = true;
                    ff.writeBit          = 0;
                    ff.messageType       = static_cast<byte>(FujiMessageType::STATUS);
                    
                } else {
                    if(controllerIsPrimary) {
                        // if this is the first message we have received, announce ourselves to the indoor unit
                        ff.messageSource     = controllerAddress;
                        ff.messageDest       = static_cast<byte>(FujiAddress::UNIT);
                        ff.loginBit          = false;
                        ff.controllerPresent = 0;
                        ff.updateMagic       = 0;
                        ff.unknownBit        = true;
                        ff.writeBit          = 0;
                        ff.messageType       = static_cast<byte>(FujiMessageType::LOGIN);
                        
                        ff.onOff             = 0;
                        ff.temperature       = 0;
                        ff.acMode            = 0;
                        ff.fanMode           = 0;
                        ff.swingMode         = 0;
                        ff.swingStep         = 0;
                        ff.acError           = 0;
                    } else {
                        // secondary controller never seems to get any other message types, only status with controllerPresent == 0
                        // the secondary controller seems to send the same flags no matter which message type
                        
                        ff.messageSource     = controllerAddress;
                        ff.messageDest       = static_cast<byte>(FujiAddress::UNIT);
                        ff.loginBit          = false;
                        ff.controllerPresent = 1;
                        ff.updateMagic       = 2;
                        ff.unknownBit        = true;
                        ff.writeBit          = 0;
                    }
                    
                }
                
                // if we have any updates, set the flags
                if(updateFields) {
                    ff.writeBit = 1;
                }
                
                if(updateFields & kOnOffUpdateMask) {
                    ff.onOff = updateState.onOff;
                }
                
                if(updateFields & kTempUpdateMask) {
                    ff.temperature = updateState.temperature;
                }
                
                if(updateFields & kModeUpdateMask) {
                    ff.acMode = updateState.acMode;
                }
                
                if(updateFields & kFanModeUpdateMask) {
                    ff.fanMode = updateState.fanMode;
                }
                
                if(updateFields & kSwingModeUpdateMask) {
                    ff.swingMode = updateState.swingMode;
                }
                
                if(updateFields & kSwingStepUpdateMask) {
                    ff.swingStep = updateState.swingStep;
                }

                if(updateFields & kEconomyModeUpdateMask) {
                    ff.economyMode = updateState.economyMode;
                }

                applyControllerTempOverride(ff);
                
                memcpy(&currentState, &ff, sizeof(FujiFrame));

            }
            else if(ff.messageType == static_cast<byte>(FujiMessageType::LOGIN)){
                ff.messageSource     = controllerAddress;
                ff.writeBit          = 0;       // change 1 → 0
                ff.unknownBit        = true;
                ff.updateMagic       = 0;

                if (ff.acMode == 7) {
                    // received a login frame OK frame
                    // the primary will send packet to a secondary controller to see if it exists
                    ff.messageDest       = static_cast<byte>(FujiAddress::SECONDARY);           
                    ff.messageType       = static_cast<byte>(FujiMessageType::STATUS);  // But MessageType should be = 0
                    ff.loginBit          = true;
                    ff.controllerPresent = 1;                    
                    
                    ff.onOff             = currentState.onOff;
                    ff.temperature       = currentState.temperature;
                    ff.acMode            = currentState.acMode;
                    ff.fanMode           = currentState.fanMode;
                    ff.swingMode         = currentState.swingMode;
                    ff.swingStep         = currentState.swingStep;
                    ff.acError           = currentState.acError;
                } else { 
                    // I'm not sure if this section is needed, since it has never been used in my situation, 
                    // since the handshake takes place via STATUS, and only acMode==7 works in the LOGIN section.
                    ff.messageDest       = static_cast<byte>(FujiAddress::UNIT);
                    ff.messageType       = static_cast<byte>(FujiMessageType::LOGIN);
                    ff.loginBit          = false;
                    ff.controllerPresent = 0;   

                    ff.onOff             = 0;
                    ff.temperature       = 0;
                    ff.acMode            = 0;
                    ff.fanMode           = 0;
                    ff.swingMode         = 0;
                    ff.swingStep         = 0;
                    ff.acError           = 0;
                }

                applyControllerTempOverride(ff);
            } else if(ff.messageType == static_cast<byte>(FujiMessageType::ERROR)) {
                Print& output = getDebugOutput();
                output.print("AC ERROR RECV: ");
                printFrame(output, readBuf, ff);
                // handle errors here
                return false;
            }
            
            encodeFrame(ff);

            if(debugPrint) {
                Print& output = getDebugOutput();
                output.print("<-- ");
                printFrame(output, writeBuf, ff);
            }

            for(int i=0;i<8;i++) {
                writeBuf[i] ^= 0xFF;
            }
                    
            pendingFrame = true;
                        

        } else if (ff.messageDest == static_cast<byte>(FujiAddress::SECONDARY)) {
            seenSecondaryController = true;
            if (!controllerTempOverrideEnabled) {
                currentState.controllerTemp = ff.controllerTemp;
            }
        }
        
        return true;
    }
    
    return false;
}

bool FujiHeatPump::isBound() {
    if(hasReceivedFrame() && millis() - lastFrameReceived < kBoundTimeoutMs) {
        return true;
    }
    return false;
}

bool FujiHeatPump::updatePending() {
    if(updateFields) {
        return true;
    }
    return false;
}

bool FujiHeatPump::hasReceivedFrame() {
    return lastFrameReceived != 0;
}

unsigned long FujiHeatPump::getLastFrameAgeMs() {
    if(!hasReceivedFrame()) {
        return 0;
    }

    return millis() - lastFrameReceived;
}

bool FujiHeatPump::hasSeenPrimaryController() {
    return seenPrimaryController;
}

bool FujiHeatPump::hasSeenSecondaryController() {
    return seenSecondaryController;
}

bool FujiHeatPump::isPrimaryController() {
    return controllerIsPrimary;
}

byte FujiHeatPump::getControllerAddress() {
    return controllerAddress;
}

bool FujiHeatPump::hasPendingFrame() {
    return pendingFrame;
}

void FujiHeatPump::setOnOff(bool o){
    updateFields |= kOnOffUpdateMask;
    updateState.onOff = o ? 1 : 0;   
}
void FujiHeatPump::setTemp(byte t){
    updateFields |= kTempUpdateMask;
    updateState.temperature = t;
}
void FujiHeatPump::setMode(byte m){
    updateFields |= kModeUpdateMask;
    updateState.acMode = m;
}
void FujiHeatPump::setFanMode(byte fm){
    updateFields |= kFanModeUpdateMask;
    updateState.fanMode = fm;
}
void FujiHeatPump::setEconomyMode(byte em){
    updateFields |= kEconomyModeUpdateMask;
    updateState.economyMode = em;
}
void FujiHeatPump::setSwingMode(byte sm){
    updateFields |= kSwingModeUpdateMask;
    updateState.swingMode = sm;
}
void FujiHeatPump::setSwingStep(byte ss){
    updateFields |= kSwingStepUpdateMask;
    updateState.swingStep = ss;  
}

void FujiHeatPump::setDebug(bool isOn) {
    debugPrint = isOn;
}

void FujiHeatPump::setDebugOutput(Print* output) {
    debugOutput = output;
}

void FujiHeatPump::setControllerRole(bool primary) {
    controllerIsPrimary = primary;
    controllerAddress = primary
        ? static_cast<byte>(FujiAddress::PRIMARY)
        : static_cast<byte>(FujiAddress::SECONDARY);

    // Role switch changes our bus identity, so old handshake/update state is stale.
    seenPrimaryController = false;
    seenSecondaryController = false;
    lastFrameReceived = 0;
    updateFields = 0;
    pendingFrame = false;
}

void FujiHeatPump::setControllerTempOverride(bool enabled, byte temperature) {
    controllerTempOverrideEnabled = enabled;

    if (!enabled) {
        return;
    }

    controllerTempOverride = constrain(temperature, (byte)0, (byte)63);
    currentState.controllerTemp = controllerTempOverride;
}

bool FujiHeatPump::getOnOff(){
    return currentState.onOff == 1 ? true : false;
}
byte FujiHeatPump::getTemp(){
    return currentState.temperature;
}
byte FujiHeatPump::getMode(){
    return currentState.acMode;
}
byte FujiHeatPump::getFanMode(){
    return currentState.fanMode;
}
byte FujiHeatPump::getEconomyMode(){
    return currentState.economyMode;
}
byte FujiHeatPump::getSwingMode(){
    return currentState.swingMode;
}
byte FujiHeatPump::getSwingStep(){
    return currentState.swingStep;
}
byte FujiHeatPump::getControllerTemp(){
    return currentState.controllerTemp;
}

FujiFrame *FujiHeatPump::getCurrentState(){
    return &currentState;
}

FujiFrame *FujiHeatPump::getUpdateState(){
    return &updateState;
}

byte FujiHeatPump::getUpdateFields(){
    return updateFields;
}
 
