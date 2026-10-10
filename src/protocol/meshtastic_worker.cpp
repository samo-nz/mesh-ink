#include "meshtastic_worker.h"
#include "meshtastic_official_phoneapi.h"
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

QueueHandle_t rx_free=nullptr;
QueueHandle_t rx_ready=nullptr;
QueueHandle_t tx_pending=nullptr;
QueueHandle_t tx_failed=nullptr;
SemaphoreHandle_t worker_exited=nullptr;
TaskHandle_t worker_task=nullptr;
RxFrame* rx_pool[kRxFrames]{};
std::atomic<bool> stop_requested{false};
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
    if(worker_exited){vSemaphoreDelete(worker_exited);worker_exited=nullptr;}
    for(auto& frame:rx_pool){heap_caps_free(frame);frame=nullptr;}
    worker_task=nullptr;
    running=false;
}

void network_task(void*){
    const TickType_t pause=pdMS_TO_TICKS(5)?pdMS_TO_TICKS(5):1;
    uint32_t last_gps=0,last_slow_report=0;
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
        if(!last_gps || (uint32_t)(now-last_gps)>=250){
            last_gps=now;
            meshink_meshtastic_native_gps_update();
        }
        meshink_meshtastic_native_loop();

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
    worker_exited=xSemaphoreCreateBinary();
    if(!rx_free||!rx_ready||!tx_pending||!tx_failed||!worker_exited){
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
