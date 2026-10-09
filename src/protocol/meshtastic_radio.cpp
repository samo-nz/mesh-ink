#include "meshtastic_radio.h"
#include "../hardware/radio.h"
#include "../../lib/Meshtastic/src/mesh/SX1262Interface.h"
#include "../../lib/Meshtastic/src/mesh/Router.h"
#include "../../lib/Meshtastic/src/SPILock.h"
#include <Arduino.h>
#include <SPI.h>
#include <memory>
#include <new>

namespace {
// RadioLib's HAL is borrowed by the official native radio instance and must
// outlive it. This exists only when the Meshtastic protocol starts.
std::unique_ptr<LockingArduinoHal> native_hal;
}

std::unique_ptr<RadioInterface> meshink_meshtastic_create_radio() {
    // The selected protocol must own the SX1262. Do not call MeshCore's
    // meshink_radio_initialize(), CustomSX1262Wrapper, or the shared PhysicalLayer.
    if (!spiLock) {
        Serial.println("[MeshInk/MT] SPI locking service is not initialized");
        return nullptr;
    }
    meshink_radio_prepare_native_spi_bus();
    const MeshInkSX126xModuleConfig hw=meshink_radio_native_module_config();
    if(hw.chip_select<0||hw.dio1<0||hw.reset<0||hw.busy<0||
       hw.tcxo_voltage<=0.0f){
        Serial.println("[MeshInk/MT] Invalid SX1262 hardware description");
        return nullptr;
    }
    if(!native_hal){
        native_hal.reset(new(std::nothrow) LockingArduinoHal(
            meshink_radio_native_spi_bus(),
            SPISettings(4000000,MSBFIRST,SPI_MODE0)));
        if(!native_hal)return nullptr;
    }
    std::unique_ptr<SX1262Interface> radio(new(std::nothrow) SX1262Interface(
        native_hal.get(),(RADIOLIB_PIN_TYPE)hw.chip_select,
        (RADIOLIB_PIN_TYPE)hw.dio1,(RADIOLIB_PIN_TYPE)hw.reset,
        (RADIOLIB_PIN_TYPE)hw.busy));
    if(!radio)return nullptr;
    radio->setTCXOVoltage(hw.tcxo_voltage);
    // Upstream init() configures and arms native IRQ-driven RX, and applies
    // the Meshtastic modem's own synchronisation, CAD and transmission rules.
    if(!radio->init()){
        Serial.println("[MeshInk/MT] Official SX1262 radio init failed");
        return nullptr;
    }
    return std::unique_ptr<RadioInterface>(std::move(radio));
}

bool meshink_meshtastic_attach_radio(Router& native_router) {
    std::unique_ptr<RadioInterface> radio=meshink_meshtastic_create_radio();
    if(!radio)return false;
    native_router.addInterface(std::move(radio));
    Serial.println("[MeshInk/MT] Official SX1262 driver attached to Router");
    return true;
}
