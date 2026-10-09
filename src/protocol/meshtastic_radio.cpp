#include "meshtastic_radio.h"
#include "../hardware/radio.h"
#include "../../lib/Meshtastic/src/mesh/SX1262Interface.h"
#include "../../lib/Meshtastic/src/mesh/Router.h"
#include "../../lib/Meshtastic/src/SPILock.h"
#include <Arduino.h>
#include <memory>
#include <new>

namespace {
// RadioLib's HAL is borrowed by the official native radio instance and must
// outlive it. This exists only when the Meshtastic protocol starts.
std::unique_ptr<LockingArduinoHal> native_hal;

// The T5 RF switch must be enabled AFTER SX1262 begin(), as in MeshCore's
// proven LilyGO post-init sequence. Keep upstream RF modem/IRQ logic intact.
// Board capabilities are supplied by the MeshInk generic radio descriptor,
// never hardcoded to a GPIO or chip frequency in a protocol helper.
class MeshInkNativeSX1262 final : public SX1262Interface {
    bool dio2_switch_;
    bool apply_board_rf_switch() {
        if(!dio2_switch_)return true;
        const int16_t result=lora.setDio2AsRfSwitch(true);
        if(result!=RADIOLIB_ERR_NONE)
            Serial.printf("[MeshInk/MT] DIO2 RF switch error %d\\n",(int)result);
        return result==RADIOLIB_ERR_NONE;
    }
public:
    MeshInkNativeSX1262(LockingArduinoHal* hal,RADIOLIB_PIN_TYPE cs,
                        RADIOLIB_PIN_TYPE irq,RADIOLIB_PIN_TYPE rst,
                        RADIOLIB_PIN_TYPE busy,bool dio2)
        : SX1262Interface(hal,cs,irq,rst,busy),dio2_switch_(dio2){}
    bool init() override {
        if(!SX1262Interface::init())return false;
        return apply_board_rf_switch();
    }
    bool reconfigure() override {
        if(!SX1262Interface::reconfigure())return false;
        return apply_board_rf_switch();
    }
};
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
    std::unique_ptr<MeshInkNativeSX1262> radio(new(std::nothrow) MeshInkNativeSX1262(
        native_hal.get(),(RADIOLIB_PIN_TYPE)hw.chip_select,
        (RADIOLIB_PIN_TYPE)hw.dio1,(RADIOLIB_PIN_TYPE)hw.reset,
        (RADIOLIB_PIN_TYPE)hw.busy,hw.dio2_rf_switch));
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
