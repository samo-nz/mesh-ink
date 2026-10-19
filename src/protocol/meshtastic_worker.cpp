#include "meshtastic_worker.h"
#include "meshtastic_official_phoneapi.h"
#include "meshtastic_radio.h"
#include "meshtastic_runtime.h"
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/mesh.pb.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <atomic>
#include <cstring>

namespace {
// Pin the network to core 0, independently from Arduino's UI loop on core 1.
// MeshInk's shared touch sampler runs on core 0 at a higher priority (2).
constexpr UBaseType_t kNetworkPriority=1;
constexpr BaseType_t kNetworkCore=0;
constexpr uint32_t kNetworkStackBytes=12288;
constexpr UBaseType_t kRxFrames=12;
constexpr UBaseType_t kTxFrames=12;
constexpr UBaseType_t kRxPerPass=4;
constexpr UBaseType_t kTxPerPass=4;

struct RxFrame {
    size_t size;
    uint8_t wire[meshtastic_FromRadio_size];
};
struct TxFrame {
    size_t size;
    uint32_t packet_id;
    uint8_t wire[meshtastic_ToRadio_size];
};
// Only the native worker sends events into this bounded cross-core FIFO.
// No PhoneAPI or router state is ever inspected by the UI core.
struct RetryEvent {
    uint32_t packet_id;
    uint8_t retry;
    uint8_t total;
};

QueueHandle_t rx_free=nullptr;
QueueHandle_t rx_ready=nullptr;
QueueHandle_t tx_pending=nullptr;
QueueHandle_t tx_failed=nullptr;
QueueHandle_t retry_events=nullptr;
QueueHandle_t quick_events=nullptr;
SemaphoreHandle_t worker_exited=nullptr;
TaskHandle_t worker_task=nullptr;
RxFrame* rx_pool[kRxFrames]{};
std::atomic<bool> stop_requested{false};
std::atomic<bool> config_refresh_requested{false};
std::atomic<uint8_t> quick_broadcast_request{0}; // 0 idle, 1 position, 2 NodeInfo
std::atomic<bool> quick_location_allowed{false};
std::atomic<bool> quick_native_position_available{false};
bool running=false;

void release_resources(){
    TxFrame* pending=nullptr;
    if(tx_pending){
        while(xQueueReceive(tx_pending,&pending,0)==pdTRUE)
            heap_caps_free(pending);
        vQueueDelete(tx_pending);tx_pending=nullptr;
    }
    if(rx_free){vQueueDelete(rx_free);rx_free=nullptr;}
    if(rx_ready){vQueueDelete(rx_ready);rx_ready=nullptr;}
    if(tx_failed){vQueueDelete(tx_failed);tx_failed=nullptr;}
    if(retry_events){vQueueDelete(retry_events);retry_events=nullptr;}
    if(quick_events){vQueueDelete(quick_events);quick_events=nullptr;}
    if(worker_exited){vSemaphoreDelete(worker_exited);worker_exited=nullptr;}
    for(auto& frame:rx_pool){heap_caps_free(frame);frame=nullptr;}
    worker_task=nullptr;
    running=false;
}

void network_task(void*){
    const TickType_t pause=pdMS_TO_TICKS(5)?pdMS_TO_TICKS(5):1;
    uint32_t last_gps=0,last_slow_report=0;
    uint32_t refresh_at=0;
    uint32_t last_quick_sent_ms=0;
    bool quick_ever_sent=false;
    while(!stop_requested.load(std::memory_order_acquire)){
        const uint32_t started=millis();

        // No UI task ever calls into the live official PhoneAPI.
        TxFrame* tx=nullptr;
        for(unsigned i=0;i<kTxPerPass && xQueueReceive(tx_pending,&tx,0)==pdTRUE;++i){
            const bool accepted=meshink_official_phoneapi_submit(tx->wire,tx->size);
            if(!accepted){
                Serial.printf("[MT-WORKER] ToRadio rejected id=%08lx\n",
                              (unsigned long)tx->packet_id);
                if(tx->packet_id){
                    const uint32_t id=tx->packet_id;
                    if(xQueueSend(tx_failed,&id,0)!=pdTRUE)
                        Serial.println("[MT-WORKER] ERROR: TX failure queue full");
                }
            }
            heap_caps_free(tx);
        }

        const uint32_t now=millis();
        if(config_refresh_requested.exchange(false,std::memory_order_acq_rel))
            refresh_at=now+800UL;
        if(!last_gps || (uint32_t)(now-last_gps)>=250){
            last_gps=now;
            meshink_meshtastic_native_gps_update();
        }
        meshink_meshtastic_native_loop();
        // The UI reads this atomic snapshot, never the mutable official
        // NodeDB, config or position-module objects on another CPU core.
        quick_native_position_available.store(
            meshink_meshtastic_native_quick_position_available(),
            std::memory_order_release);
        // Only the network worker may invoke native Meshtastic modules.
        // Recheck privacy after GPS update, even if the user disabled sharing
        // while a previous touch-driven request was pending.
        const uint8_t quick=quick_broadcast_request.exchange(0,std::memory_order_acq_rel);
        if(quick){
            const bool privacy_ok=quick!=1||quick_location_allowed.load(std::memory_order_acquire);
            const bool cooldown=quick_ever_sent&&
                (uint32_t)(millis()-last_quick_sent_ms)<30000UL;
            if(!privacy_ok){
                const uint8_t result=3; // privacy revoked after user tap
                (void)xQueueSend(quick_events,&result,0);
            }else if(cooldown||
                     (quick==2&&meshink_meshtastic_native_quick_nodeinfo_busy())||
                     !meshink_meshtastic_radio_quick_begin(quick)){
                const uint8_t result=4; // radio pending, or cooldown after real TX
                (void)xQueueSend(quick_events,&result,0);
            }else{
                const bool native_accepted=meshink_meshtastic_native_quick_broadcast(quick==1);
                meshink_meshtastic_radio_quick_finish(native_accepted);
            }
        }
        uint8_t result_kind=0;
        bool transmitted=false;
        if(meshink_meshtastic_radio_quick_result(result_kind,transmitted)){
            const uint8_t result=transmitted?result_kind:3;
            if(transmitted){
                last_quick_sent_ms=millis();
                quick_ever_sent=true;
            }
            if(xQueueSend(quick_events,&result,0)!=pdTRUE)
                Serial.println("[MT-QUICK] ERROR: result queue full");
            Serial.printf("[MT-QUICK] radio outcome kind=%u transmitted=%u\n",
                          (unsigned)result_kind,transmitted?1U:0U);
        }

        // Local channel add/remove is submitted through upstream AdminModule.
        // Only after the official router has applied the write, re-request the
        // complete native PhoneAPI snapshot. Never optimistically discard a
        // channel that may have been rejected.
        if(refresh_at && (int32_t)(millis()-refresh_at)>=0){
            refresh_at=0;
            const bool accepted=meshink_official_phoneapi_open(1);
            Serial.printf("[MT-WORKER] channel config refresh %s\\n",
                          accepted?"queued":"FAILED");
        }

        // Reserve a free slot BEFORE removing anything from PhoneAPI.
        // A slow e-paper update backpressures the UI handoff rather than
        // silently discarding config, node information, or messages.
        for(unsigned i=0;i<kRxPerPass && meshink_official_phoneapi_has_data();++i){
            RxFrame* rx=nullptr;
            if(xQueueReceive(rx_free,&rx,0)!=pdTRUE)break;
            rx->size=meshink_official_phoneapi_receive(rx->wire,sizeof(rx->wire));
            if(!rx->size){
                xQueueSend(rx_free,&rx,0);
                break;
            }
            if(xQueueSend(rx_ready,&rx,0)!=pdTRUE){
                // Invariant: all outstanding RX frames come from this pool.
                Serial.println("[MT-WORKER] ERROR: RX queue invariant violated");
                xQueueSend(rx_free,&rx,0);
                break;
            }
        }

        const uint32_t elapsed=millis()-started;
        if(elapsed>100 && (!last_slow_report || (uint32_t)(millis()-last_slow_report)>=2000)){
            last_slow_report=millis();
            Serial.printf("[MT-WORKER] slow pass=%lums rx=%u/%u tx=%u/%u stack_words=%u\n",
                          (unsigned long)elapsed,(unsigned)uxQueueMessagesWaiting(rx_ready),
                          (unsigned)kRxFrames,(unsigned)uxQueueMessagesWaiting(tx_pending),
                          (unsigned)kTxFrames,(unsigned)uxTaskGetStackHighWaterMark(nullptr));
        }
        vTaskDelay(pause);
    }
    // The worker owns shutdown to avoid racing radio/PhoneAPI with MeshInk.
    meshink_official_phoneapi_close();
    meshink_meshtastic_native_stop();
    xSemaphoreGive(worker_exited);
    vTaskDelete(nullptr);
}
} // namespace

bool meshink_meshtastic_worker_start(){
    if(running)return true;
    rx_free=xQueueCreate(kRxFrames,sizeof(RxFrame*));
    rx_ready=xQueueCreate(kRxFrames,sizeof(RxFrame*));
    tx_pending=xQueueCreate(kTxFrames,sizeof(TxFrame*));
    tx_failed=xQueueCreate(kTxFrames,sizeof(uint32_t));
    retry_events=xQueueCreate(kTxFrames,sizeof(RetryEvent));
    quick_events=xQueueCreate(4,sizeof(uint8_t));
    worker_exited=xSemaphoreCreateBinary();
    if(!rx_free||!rx_ready||!tx_pending||!tx_failed||!retry_events||!quick_events||!worker_exited){
        Serial.println("[MT-WORKER] ERROR: queues/semaphore allocation failed");
        release_resources();return false;
    }
    for(auto& frame:rx_pool){
        frame=static_cast<RxFrame*>(
            heap_caps_malloc(sizeof(RxFrame),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!frame||xQueueSend(rx_free,&frame,0)!=pdTRUE){
            Serial.println("[MT-WORKER] ERROR: PSRAM receive pool allocation failed");
            release_resources();return false;
        }
    }
    stop_requested.store(false,std::memory_order_release);
    config_refresh_requested.store(false,std::memory_order_release);
    quick_broadcast_request.store(0,std::memory_order_release);
    quick_location_allowed.store(false,std::memory_order_release);
    quick_native_position_available.store(false,std::memory_order_release);
    if(xTaskCreatePinnedToCore(network_task,"mt-network",kNetworkStackBytes,
                               nullptr,kNetworkPriority,&worker_task,
                               kNetworkCore)!=pdPASS){
        Serial.println("[MT-WORKER] ERROR: unable to start network task");
        release_resources();return false;
    }
    running=true;
    Serial.printf("[MT-WORKER] started core=%d priority=%u RX=%u TX=%u\n",
                  (int)kNetworkCore,(unsigned)kNetworkPriority,
                  (unsigned)kRxFrames,(unsigned)kTxFrames);
    return true;
}

bool meshink_meshtastic_worker_submit(const uint8_t* bytes,size_t size,uint32_t packet_id){
    if(!running||!bytes||!size||size>meshtastic_ToRadio_size)return false;
    auto* tx=static_cast<TxFrame*>(
        heap_caps_malloc(sizeof(TxFrame),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!tx)return false;
    tx->size=size;
    tx->packet_id=packet_id;
    memcpy(tx->wire,bytes,size);
    if(xQueueSend(tx_pending,&tx,0)!=pdTRUE){
        heap_caps_free(tx);
        Serial.println("[MT-WORKER] TX queue full; request not accepted");
        return false;
    }
    return true;
}

bool meshink_meshtastic_worker_quick_position_available(){
    return running&&quick_native_position_available.load(std::memory_order_acquire);
}
bool meshink_meshtastic_worker_quick_broadcast(bool location){
    if(!running||stop_requested.load(std::memory_order_acquire))return false;
    if(location&&(!quick_location_allowed.load(std::memory_order_acquire)||
                  !quick_native_position_available.load(std::memory_order_acquire)))return false;
    uint8_t empty=0;
    return quick_broadcast_request.compare_exchange_strong(
        empty,location?1U:2U,std::memory_order_acq_rel);
}
bool meshink_meshtastic_worker_quick_result(uint8_t& result){
    return running&&quick_events&&xQueueReceive(quick_events,&result,0)==pdTRUE;
}
bool meshink_meshtastic_worker_quick_location_allowed(){
    return quick_location_allowed.load(std::memory_order_acquire);
}
void meshink_meshtastic_worker_allow_quick_location(bool allowed){
    quick_location_allowed.store(allowed,std::memory_order_release);
    if(!allowed){
        uint8_t expected=1;
        quick_broadcast_request.compare_exchange_strong(expected,0,std::memory_order_acq_rel);
    }
}

void meshink_meshtastic_worker_refresh_channels(){
    if(running)config_refresh_requested.store(true,std::memory_order_release);
}

size_t meshink_meshtastic_worker_receive(uint8_t* bytes,size_t capacity){
    if(!running||!bytes||capacity<meshtastic_FromRadio_size)return 0;
    RxFrame* rx=nullptr;
    if(xQueueReceive(rx_ready,&rx,0)!=pdTRUE)return 0;
    const size_t n=rx->size<=capacity?rx->size:0;
    if(n)memcpy(bytes,rx->wire,n);
    // Returning the slot cannot overwrite the UI's already-copied bytes.
    xQueueSend(rx_free,&rx,0);
    return n;
}

bool meshink_meshtastic_worker_tx_failed(uint32_t& packet_id){
    return running&&xQueueReceive(tx_failed,&packet_id,0)==pdTRUE;
}

bool meshink_meshtastic_worker_note_retry(uint32_t packet_id,uint8_t retry,uint8_t total){
    if(!retry_events||!packet_id||!retry||retry>total)return false;
    const RetryEvent event{packet_id,retry,total};
    // If the UI is in a slow e-paper refresh, leave the router's previous
    // seen-count unchanged so it can re-offer this same update next cycle.
    return xQueueSend(retry_events,&event,0)==pdTRUE;
}

bool meshink_meshtastic_worker_retry_status(uint32_t& packet_id,uint8_t& retry,uint8_t& total){
    if(!running||!retry_events)return false;
    RetryEvent event{};
    if(xQueueReceive(retry_events,&event,0)!=pdTRUE)return false;
    packet_id=event.packet_id;
    retry=event.retry;
    total=event.total;
    return true;
}

bool meshink_meshtastic_worker_stop(){
    if(!running)return true;
    stop_requested.store(true,std::memory_order_release);
    // Shutdown is not part of the interactive typing path.
    if(xSemaphoreTake(worker_exited,pdMS_TO_TICKS(3000))!=pdTRUE){
        Serial.println("[MT-WORKER] ERROR: worker stop timeout; preserving its resources");
        return false;
    }
    release_resources();
    Serial.println("[MT-WORKER] stopped cleanly");
    return true;
}
