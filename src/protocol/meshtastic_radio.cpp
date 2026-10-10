#include "meshtastic_radio.h"
#include "meshtastic_sx1262_instance.cpp" // actual official SX1262 implementation
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
class MeshInkNativeSX1262;
MeshInkNativeSX1262* active_native_radio=nullptr;

// The T5 RF switch must be enabled AFTER SX1262 begin(), as in MeshCore's
// proven LilyGO post-init sequence. Keep upstream RF modem/IRQ logic intact.
// Board capabilities are supplied by the MeshInk generic radio descriptor,
// never hardcoded to a GPIO or chip frequency in a protocol helper.
class MeshInkNativeSX1262 final : public SX1262Interface {
    bool dio2_switch_;
    float board_tcxo_voltage_;
    bool apply_board_settings() {
        // Match the working MeshCore H752 sequence exactly: start the chip
        // on RadioLib's 1.6 V TCXO, change the fitted TCXO to 2.4 V, and
        // finally enable the DIO2-controlled RF switch.
        const int16_t tcxo=lora.setTCXO(board_tcxo_voltage_);
        if(tcxo!=RADIOLIB_ERR_NONE){
            Serial.printf("[MeshInk/MT] Post-init TCXO error %d\\n",(int)tcxo);
            return false;
        }
        setTCXOVoltage(board_tcxo_voltage_); // reconfigure/recovery setting
        if(!dio2_switch_){startReceive();return true;}
        const int16_t rf=lora.setDio2AsRfSwitch(true);
        if(rf!=RADIOLIB_ERR_NONE)
            Serial.printf("[MeshInk/MT] Post-init DIO2 switch error %d\\n",(int)rf);
        if(rf!=RADIOLIB_ERR_NONE)return false;
        // Start listening *after* the fitted TCXO and RF switch are set.
        // The stock init() arms RX early; re-arm with final board electrical state.
        startReceive();
        return true;
    }
  protected:
    // Upstream's chip recovery re-runs begin(), clearing DIO2 switching.
    // Reapply the board electrical settings on recovery too, without changing
    // the initial begin-before-DIO2 ordering shared with working MeshCore.
    bool recoverChipStateLoss() override {
        // Upstream's private chip-recovery helper cannot be called from a
        // board subclass. Its public reconfigure() has the same guarded
        // standby -> modem-params -> full-reinit fallback used by recovery.
        if(!SX1262Interface::reconfigure())return false;
        return apply_board_settings();
    }
public:
    void report() const {
#if MESHINK_MESHTASTIC_HW_TEST_LOG
        Serial.printf("[MT-TEST] radio stats rx_ok=%lu rx_bad=%lu tx_ok=%lu tx_relay=%lu tx_drop=%u rx_armed=%u rx_offline=%u recovery_attempts=%u\n",
                      (unsigned long)rxGood,(unsigned long)rxBad,(unsigned long)txGood,
                      (unsigned long)txRelay,(unsigned)txDrop,isReceiving?1U:0U,
                      rxOffline?1U:0U,(unsigned)chipRecoveryFailures);
#endif
    }
    MeshInkNativeSX1262(LockingArduinoHal* hal,RADIOLIB_PIN_TYPE cs,
                        RADIOLIB_PIN_TYPE irq,RADIOLIB_PIN_TYPE rst,
                        RADIOLIB_PIN_TYPE busy,float tcxo,bool dio2)
        : SX1262Interface(hal,cs,irq,rst,busy),dio2_switch_(dio2),
          board_tcxo_voltage_(tcxo){}
    bool init() override {
        setTCXOVoltage(1.6f);
        if(!SX1262Interface::init())return false;
        return apply_board_settings();
    }
    bool reconfigure() override {
        if(!SX1262Interface::reconfigure())return false;
        return apply_board_settings();
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
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] SX1262 board SPI ready cs=%d irq=%d rst=%d busy=%d tcxo=%.1fV dio2_rf=%u heap=%u\n",
                  hw.chip_select,hw.dio1,hw.reset,hw.busy,(double)hw.tcxo_voltage,
                  hw.dio2_rf_switch?1U:0U,(unsigned)ESP.getFreeHeap());
#endif
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
        (RADIOLIB_PIN_TYPE)hw.busy,hw.tcxo_voltage,hw.dio2_rf_switch));
    if(!radio)return nullptr;
    // The native wrapper applies the board TCXO voltage only after begin().
    // Upstream init() configures and arms native IRQ-driven RX, and applies
    // the Meshtastic modem's own synchronisation, CAD and transmission rules.
    if(!radio->init()){
#if MESHINK_MESHTASTIC_HW_TEST_LOG
        Serial.printf("[MT-TEST] SX1262 initialization failed heap=%u; inspect upstream RadioLib errors\n",
                      (unsigned)ESP.getFreeHeap());
#endif
        Serial.println("[MeshInk/MT] Official SX1262 radio init failed");
        return nullptr;
    }
    active_native_radio=radio.get();
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.println("[MT-TEST] SX1262 initialized, post-init TCXO/DIO2 set, native RX re-armed");
    active_native_radio->report();
#endif
    return std::unique_ptr<RadioInterface>(std::move(radio));
}

bool meshink_meshtastic_attach_radio(Router& native_router) {
    std::unique_ptr<RadioInterface> radio=meshink_meshtastic_create_radio();
    if(!radio)return false;
    native_router.addInterface(std::move(radio));
    Serial.println("[MeshInk/MT] Official SX1262 driver attached to Router");
    return true;
}

void meshink_meshtastic_radio_report(){
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    if(active_native_radio)active_native_radio->report();
#endif
}
