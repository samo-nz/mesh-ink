#include "meshtastic_runtime.h"
#include "meshtastic_radio.h"
#include "../../lib/Meshtastic/src/FSCommon.h"
#include "../../lib/Meshtastic/src/SPILock.h"
#include "../../lib/Meshtastic/src/SerialConsole.h"
#include "../../lib/Meshtastic/src/PowerFSM.h"
#include "../../lib/Meshtastic/src/PowerStatus.h"
#include "../../lib/Meshtastic/src/airtime.h"
#include "../../lib/Meshtastic/src/concurrency/OSThread.h"
#include "../../lib/Meshtastic/src/mesh/NodeDB.h"
#include "../../lib/Meshtastic/src/mesh/ReliableRouter.h"
#include "../../lib/Meshtastic/src/mesh/MeshService.h"
#include "../../lib/Meshtastic/src/mesh/CryptoEngine.h"
#include "../../lib/Meshtastic/src/modules/Modules.h"
#include "../../lib/Meshtastic/src/mqtt/MQTT.h"
#include "../../lib/Meshtastic/src/main.h"
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/mesh.pb.h"
#include <Arduino.h>
#include <new>
#include <memory>
#include <cstring>

// Missing upstream-main globals: strictly the protocol engine's state.
// There is no second device application or hardware initialization.
Router* router=nullptr;
MQTT* mqtt=nullptr;              // MeshInk does not start a second network bridge
meshtastic::PowerStatus* powerStatus=nullptr;
bool pauseBluetoothLogging=false;
bool runASAP=false;
uint32_t rebootAtMsec=0;
uint32_t shutdownAtMsec=0;
bool suppressRebootBanner=true;
bool isUSBPowered=false;
uint32_t serialSinceMsec=0;
uint32_t timeLastPowered=0;
SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

// MQTT is intentionally not instantiated for this standalone e-paper client.
// Official router/PhoneAPI call these methods only when mqtt is non-null.
void MQTT::onSend(const meshtastic_MeshPacket&,const meshtastic_MeshPacket&,ChannelIndex){}
void MQTT::onClientProxyReceive(meshtastic_MqttClientProxyMessage){}

void updateBatteryLevel(uint8_t){
    // MeshInk owns the power gauge and status-bar display, not Meshtastic BLE.
}
meshtastic_DeviceMetadata getDeviceMetadata(){
    meshtastic_DeviceMetadata meta=meshtastic_DeviceMetadata_init_default;
    strncpy(meta.firmware_version,APP_VERSION,sizeof(meta.firmware_version)-1);
    meta.firmware_version[sizeof(meta.firmware_version)-1]=0;
    meta.role=config.device.role;
    meta.hw_model=owner.hw_model;
    meta.hasBluetooth=false;
    meta.hasWifi=false;
    meta.hasEthernet=false;
    meta.hasPKC=true;
    return meta;
}

namespace {
bool native_ready=false;
bool ever_started=false;
}

bool meshink_meshtastic_native_begin(){
    if(ever_started)return native_ready;
    ever_started=true;
    // Mirror the official firmware's initialization order, without running
    // any of its display, phone BLE, USB, GPS or power application startup.
    concurrency::hasBeenSetup=true;
    concurrency::OSThread::setup();
    initSPI();
    consoleInit();
    fsInit(); // uses MeshInk's already-mounted SPIFFS, no autoformat
    nodeDB=new(std::nothrow) NodeDB();
    if(!nodeDB){
        Serial.println("[MeshInk/MT] NodeDB failed");
        return false;
    }
    router=new(std::nothrow) ReliableRouter();
    service=new(std::nothrow) MeshService();
    if(!router||!service){
        Serial.println("[MeshInk/MT] Router or MeshService allocation failed");
        return false;
    }
    service->init();
    setupModules();
    if(!airTime)airTime=new(std::nothrow) AirTime();
    if(!airTime){
        Serial.println("[MeshInk/MT] Airtime allocator failed");
        return false;
    }
    if(!powerStatus)powerStatus=new(std::nothrow) meshtastic::PowerStatus();
    PowerFSM_setup(); // official excluded-power-FSM no-op, MeshInk owns sleep
    native_ready=meshink_meshtastic_attach_radio(*router);
    if(!native_ready)Serial.println("[MeshInk/MT] Native radio initialization failed");
    else Serial.println("[MeshInk/MT] Native Meshtastic node initialized");
    return native_ready;
}

void meshink_meshtastic_native_loop(){
    if(!native_ready)return;
    runASAP=false;
    // Meshtastic OSThread runs the genuine router, radio and protocol modules.
    // Keep MeshInk's own UI/event scheduler in unified_main.
    concurrency::mainController.runOrDelay();
    service->loop();
}

void meshink_meshtastic_native_stop(){
    // Hardware is exclusively owned by the boot-selected router. Final shutdown
    // is followed by the MeshInk reboot-to-switch; no hot swapping of ISR state.
    native_ready=false;
}
