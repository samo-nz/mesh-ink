#include "meshtastic_runtime.h"
#include "meshtastic_platform.cpp" // headless service definitions (single TU)
#include "meshtastic_radio.h"
#include "../../lib/Meshtastic/src/FSCommon.h"
#include "../../lib/Meshtastic/src/SPILock.h"
#include "../../lib/Meshtastic/src/PowerMon.h"
#include "../../lib/Meshtastic/src/mesh/TransmitHistory.h"
#include "../../lib/Meshtastic/src/PowerFSM.h"
#include "../../lib/Meshtastic/src/PowerStatus.h"
#include "../../lib/Meshtastic/src/airtime.h"
#include "../../lib/Meshtastic/src/concurrency/OSThread.h"
#include "../../lib/Meshtastic/src/mesh/NodeDB.h"
#include "../../lib/Meshtastic/src/mesh/ReliableRouter.h"
#include "../../lib/Meshtastic/src/mesh/MeshService.h"
#include "../../lib/Meshtastic/src/mesh/CryptoEngine.h"
#include "../../lib/Meshtastic/src/mesh/RadioLibInterface.h"
#include "../../lib/Meshtastic/src/mesh/Throttle.h"
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
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] engine boot: initial heap=%u psram=%u\n",
                  (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());
#endif
    // Mirror the official firmware's initialization order, without running
    // any of its display, phone BLE, USB, GPS or power application startup.
    concurrency::hasBeenSetup=true;
    concurrency::OSThread::setup();
    initSPI();
    // MeshInk owns USB serial; use only the in-process official PhoneAPI.
    powerMonInit();
    serialSinceMsec=millis();
    fsInit(); // uses MeshInk's already-mounted SPIFFS, no autoformat
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] official SPI lock, power monitor, SPIFFS initialized; heap=%u\n",
                  (unsigned)ESP.getFreeHeap());
#endif
    nodeDB=new(std::nothrow) NodeDB();
    if(!nodeDB){
        Serial.println("[MeshInk/MT] NodeDB failed");
        return false;
    }
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] NodeDB ready node=!%08lx region=%u preset=%u tx_enabled=%u heap=%u\n",
                  (unsigned long)nodeDB->getNodeNum(),(unsigned)config.lora.region,
                  (unsigned)config.lora.modem_preset,config.lora.tx_enabled?1U:0U,
                  (unsigned)ESP.getFreeHeap());
#endif
    TransmitHistory::getInstance()->loadFromDisk();
    if(nodeStatus)nodeStatus->observe(&nodeDB->newStatus);
    router=new(std::nothrow) ReliableRouter();
    service=new(std::nothrow) MeshService();
    if(!router||!service){
        Serial.println("[MeshInk/MT] Router or MeshService allocation failed");
        return false;
    }
    service->init();
    if(!airTime)airTime=new(std::nothrow) AirTime();
    if(!airTime){
        Serial.println("[MeshInk/MT] Airtime allocator failed");
        return false;
    }
    if(!powerStatus)powerStatus=new(std::nothrow) meshtastic::PowerStatus();
    if(!powerStatus)return false;
    setupModules();
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] official Router/MeshService/modules ready; heap=%u psram_free=%u\n",
                  (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());
#endif
    PowerFSM_setup(); // official excluded-power-FSM no-op, MeshInk owns sleep
    native_ready=meshink_meshtastic_attach_radio(*router);
    if(!native_ready)Serial.println("[MeshInk/MT] Native radio initialization failed");
    else {
        Serial.println("[MeshInk/MT] Native Meshtastic node initialized");
#if MESHINK_MESHTASTIC_HW_TEST_LOG
        nodeDB->notifyObservers(true); // Match official boot: publish initial status after radio attach.
        Serial.printf("[MT-TEST] engine READY heap=%u free_psram=%u stack_watermark_words=%u\n",
                      (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram(),
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
#endif
    }
    return native_ready;
}

void meshink_meshtastic_native_loop(){
    if(!native_ready)return;
    runASAP=false;
    // Match upstream main.cpp's radio RX/recovery upkeep. The native radio
    // handles real DIO1 IRQ, missed-edge polling and RX re-arming itself.
    if (auto* radio = RadioLibInterface::instance) {
        static uint32_t lastIrqPoll=0;
        if(!Throttle::isWithinTimespanMs(lastIrqPoll,1000)){
            lastIrqPoll=millis();
            radio->pollMissedIrqs();
        }
        static uint32_t lastMaintenance=0;
        if(!Throttle::isWithinTimespanMs(lastMaintenance,AGC_RESET_INTERVAL_MS)){
            lastMaintenance=millis();
            radio->updateNoiseFloor();
            radio->periodicRadioMaintenance();
        }
    }
    service->loop();
    // Run the genuine Meshtastic router/modules without sleeping MeshInk UI.
    concurrency::mainController.runOrDelay();
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    static uint32_t lastDiagnostic=0;
    if(!lastDiagnostic || (uint32_t)(millis()-lastDiagnostic)>=30000UL){
        lastDiagnostic=millis();
        const auto queue=router->getQueueStatus();
        Serial.printf("[MT-TEST] heartbeat uptime=%lus node=!%08lx region=%u tx=%u queue=%u/%u heap=%u psram=%u stack_words=%u\n",
                      (unsigned long)(millis()/1000UL),(unsigned long)nodeDB->getNodeNum(),
                      (unsigned)config.lora.region,config.lora.tx_enabled?1U:0U,
                      (unsigned)queue.free,(unsigned)queue.maxlen,
                      (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram(),
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        meshink_meshtastic_radio_report();
    }
#endif
}

void meshink_meshtastic_native_stop(){
    // Hardware is exclusively owned by the boot-selected router. Final shutdown
    // is followed by the MeshInk reboot-to-switch; no hot swapping of ISR state.
    native_ready=false;
}
