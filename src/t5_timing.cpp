#include "t5_timing.h"

#if T5_TIMING_DIAGNOSTICS
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>

namespace {
constexpr uint32_t LEARN_MS=30000;
constexpr uint32_t SUMMARY_MS=60000;
constexpr uint32_t EVENT_COOLDOWN_MS=5000;
constexpr uint32_t SERIAL_EVENT_SPACING_MS=2000;
constexpr uint32_t SLOW_CYCLE_FLOOR_US=500000;

enum MetricId:uint8_t {
    TouchGap=0,TouchExec,MainGap,MeshExec,UiExec,DisplayExec,CycleExec,MetricCount
};

struct Metric {
    uint64_t learn_sum=0;
    uint32_t learn_count=0;
    uint32_t learn_max=0;
    uint32_t baseline=0;
    uint32_t threshold=0;
    uint64_t window_sum=0;
    uint32_t window_count=0;
    uint32_t window_max=0;
    uint32_t excessive=0;
    uint32_t last_event_ms=0;
};

struct Event {
    MetricId metric=TouchGap;
    uint32_t observed_us=0;
    uint32_t normal_us=0;
    uint32_t limit_us=0;
    T5TimingSection section=T5TimingSection::Idle;
    uint32_t section_age_us=0;
    uint32_t at_ms=0;
};

struct CycleDetail {
    uint32_t mesh_us=0;
    uint32_t ui_us=0;
    uint32_t draw_us=0;          // longest draw_screen() in this loop
    uint32_t display_us=0;       // cumulative physical EPD time this loop
    uint32_t status_us=0;
    uint32_t text_wait_ms=0;
    uint32_t input_us=0;         // cumulative handler time for queued events
    uint32_t max_queue_age_ms=0;
    uint16_t input_events=0;
    uint8_t max_queue_depth=0;
    uint8_t display_count=0;
    uint8_t requested_mode=0;
    uint8_t actual_mode=0;
    T5UiAction trigger=T5UiAction::None;
    bool keyboard=false;
    bool landscape=false;
    bool standby=false;
    char screen[20]="?";
};

struct SlowCycleEvent {
    bool valid=false;
    uint32_t total_us=0;
    uint32_t heap=0;
    uint32_t psram=0;
    uint16_t cpu_mhz=0;
    CycleDetail detail{};
};

Metric metrics[MetricCount]{};
Event pending_event{};
SlowCycleEvent pending_slow{};
CycleDetail current_cycle{};
bool event_pending=false;
uint32_t suppressed_events=0,replaced_events=0,touch_queue_drops=0,touch_queue_drops_window=0;
uint32_t slow_cycles_window=0,slow_suppressed=0,slow_replaced=0,slow_worst_us=0,slow_queue_age_worst_ms=0;
uint32_t started_ms=0,last_summary_ms=0,last_serial_event_ms=0,last_slow_event_ms=0;
uint32_t last_touch_start_us=0,last_cycle_start_us=0;
bool learning=false;
volatile T5TimingSection current_section=T5TimingSection::Idle;
volatile uint32_t current_section_started_us=0;
portMUX_TYPE timing_mux=portMUX_INITIALIZER_UNLOCKED;

static const char* metric_name(MetricId id){
    switch(id){
        case TouchGap:return "touch-gap";
        case TouchExec:return "touch-exec";
        case MainGap:return "main-gap";
        case MeshExec:return "mesh-exec";
        case UiExec:return "ui-exec";
        case DisplayExec:return "display-exec";
        case CycleExec:return "cycle-exec";
        default:return "unknown";
    }
}

static const char* section_name(T5TimingSection section){
    switch(section){
        case T5TimingSection::Mesh:return "MESH";
        case T5TimingSection::Ui:return "UI";
        case T5TimingSection::Display:return "DISPLAY";
        default:return "IDLE";
    }
}

static const char* action_name(T5UiAction action){
    switch(action){
        case T5UiAction::Touch:return "TOUCH";
        case T5UiAction::TextRefresh:return "TEXT";
        case T5UiAction::StatusPoll:return "STATUS-POLL";
        case T5UiAction::StatusRefresh:return "STATUS-REFRESH";
        case T5UiAction::ToastRefresh:return "TOAST";
        case T5UiAction::MessageAlert:return "ALERT";
        case T5UiAction::Other:return "OTHER";
        default:return "IDLE";
    }
}

static uint32_t larger(uint32_t a,uint32_t b){return a>b?a:b;}

static uint32_t threshold_for(MetricId id,uint32_t average){
    switch(id){
        case TouchGap:return larger(50000UL,average*8UL);
        case TouchExec:return larger(25000UL,average*12UL);
        case MainGap:return larger(1500000UL,average*8UL);
        case MeshExec:return larger(100000UL,average*10UL);
        case UiExec:return larger(1200000UL,average*8UL);
        case DisplayExec:return larger(1200000UL,average*4UL);
        case CycleExec:return larger(1500000UL,average*8UL);
        default:return 100000UL;
    }
}

static bool more_severe(const Event& candidate,const Event& current){
    if(!current.limit_us)return true;
    return (uint64_t)candidate.observed_us*current.limit_us >
           (uint64_t)current.observed_us*candidate.limit_us;
}

static void queue_event_locked(MetricId id,uint32_t observed_us,uint32_t now_ms,uint32_t now_us){
    Metric& metric=metrics[id];
    metric.excessive++;
    if(metric.last_event_ms&&now_ms-metric.last_event_ms<EVENT_COOLDOWN_MS){
        suppressed_events++;
        return;
    }
    metric.last_event_ms=now_ms;
    Event candidate{};
    candidate.metric=id;
    candidate.observed_us=observed_us;
    candidate.normal_us=metric.baseline;
    candidate.limit_us=metric.threshold;
    candidate.section=current_section;
    const uint32_t section_start=current_section_started_us;
    candidate.section_age_us=section_start?(uint32_t)(now_us-section_start):0;
    candidate.at_ms=now_ms;
    if(!event_pending){
        pending_event=candidate;
        event_pending=true;
    }else if(more_severe(candidate,pending_event)){
        pending_event=candidate;
        replaced_events++;
    }else{
        suppressed_events++;
    }
}

static void adapt_baseline_locked(MetricId id,uint32_t value_us){
    Metric& metric=metrics[id];
    if(!metric.baseline){metric.baseline=value_us;}
    else metric.baseline=(uint32_t)(((uint64_t)metric.baseline*255ULL+value_us)/256ULL);
    metric.threshold=threshold_for(id,metric.baseline);
}

static void record_metric(MetricId id,uint32_t value_us){
    const uint32_t now_ms=millis();
    const uint32_t now_us=micros();
    portENTER_CRITICAL(&timing_mux);
    Metric& metric=metrics[id];
    if(learning){
        metric.learn_sum+=value_us;
        metric.learn_count++;
        if(value_us>metric.learn_max)metric.learn_max=value_us;
    }else{
        metric.window_sum+=value_us;
        metric.window_count++;
        if(value_us>metric.window_max)metric.window_max=value_us;
        if(metric.threshold&&value_us>metric.threshold)
            queue_event_locked(id,value_us,now_ms,now_us);
        else
            adapt_baseline_locked(id,value_us);
    }
    portEXIT_CRITICAL(&timing_mux);
}

static void set_section(T5TimingSection section){
    const uint32_t now=micros();
    portENTER_CRITICAL(&timing_mux);
    current_section=section;
    current_section_started_us=now;
    portEXIT_CRITICAL(&timing_mux);
}

static uint32_t learn_avg_us(const Metric& metric){
    return metric.learn_count?(uint32_t)(metric.learn_sum/metric.learn_count):0;
}

static uint32_t window_avg_us(const Metric& metric){
    return metric.window_count?(uint32_t)(metric.window_sum/metric.window_count):0;
}

static void print_ms_value(uint32_t us){
    Serial.printf("%lu.%01lums",(unsigned long)(us/1000UL),
                  (unsigned long)((us%1000UL)/100UL));
}

static uint32_t slow_cycle_threshold_locked(){
    const uint32_t learned=metrics[CycleExec].baseline;
    return larger(SLOW_CYCLE_FLOOR_US,learned?learned*12UL:0UL);
}

static void maybe_queue_slow_cycle(uint32_t elapsed_us){
    const uint32_t now=millis();
    SlowCycleEvent candidate{};
    portENTER_CRITICAL(&timing_mux);
    if(learning||elapsed_us<slow_cycle_threshold_locked()){
        portEXIT_CRITICAL(&timing_mux);
        return;
    }
    slow_cycles_window++;
    if(elapsed_us>slow_worst_us)slow_worst_us=elapsed_us;
    if(current_cycle.max_queue_age_ms>slow_queue_age_worst_ms)
        slow_queue_age_worst_ms=current_cycle.max_queue_age_ms;
    if(last_slow_event_ms&&now-last_slow_event_ms<EVENT_COOLDOWN_MS){
        slow_suppressed++;
        portEXIT_CRITICAL(&timing_mux);
        return;
    }
    last_slow_event_ms=now;
    candidate.valid=true;
    candidate.total_us=elapsed_us;
    candidate.detail=current_cycle;
    portEXIT_CRITICAL(&timing_mux);

    candidate.heap=ESP.getFreeHeap();
    candidate.psram=ESP.getFreePsram();
    candidate.cpu_mhz=(uint16_t)getCpuFrequencyMhz();

    portENTER_CRITICAL(&timing_mux);
    if(!pending_slow.valid)pending_slow=candidate;
    else if(candidate.total_us>pending_slow.total_us){
        pending_slow=candidate;
        slow_replaced++;
    }else slow_suppressed++;
    portEXIT_CRITICAL(&timing_mux);
}

static void finish_learning(){
    Metric snapshot[MetricCount]{};
    portENTER_CRITICAL(&timing_mux);
    if(!learning){
        portEXIT_CRITICAL(&timing_mux);
        return;
    }
    learning=false;
    for(uint8_t i=0;i<MetricCount;++i){
        Metric& metric=metrics[i];
        metric.baseline=learn_avg_us(metric);
        metric.threshold=threshold_for((MetricId)i,metric.baseline);
        snapshot[i]=metric;
    }
    last_summary_ms=millis();
    portEXIT_CRITICAL(&timing_mux);

    Serial.print("[T5-TIMING] learned 30s baseline: touch gap avg=");
    print_ms_value(snapshot[TouchGap].baseline);
    Serial.print(" max=");print_ms_value(snapshot[TouchGap].learn_max);
    Serial.print(" exec=");print_ms_value(snapshot[TouchExec].baseline);
    Serial.print(" | main gap=");print_ms_value(snapshot[MainGap].baseline);
    Serial.print(" mesh=");print_ms_value(snapshot[MeshExec].baseline);
    Serial.print(" ui=");print_ms_value(snapshot[UiExec].baseline);
    Serial.print(" display=");print_ms_value(snapshot[DisplayExec].baseline);
    Serial.println();

    Serial.print("[T5-TIMING] excessive limits: touch gap>");
    print_ms_value(snapshot[TouchGap].threshold);
    Serial.print(" touch exec>");print_ms_value(snapshot[TouchExec].threshold);
    Serial.print(" main gap>");print_ms_value(snapshot[MainGap].threshold);
    Serial.print(" mesh>");print_ms_value(snapshot[MeshExec].threshold);
    Serial.print(" ui>");print_ms_value(snapshot[UiExec].threshold);
    Serial.print(" display>");print_ms_value(snapshot[DisplayExec].threshold);
    Serial.println("; abnormal samples never teach the baseline");
    Serial.print("[T5-TIMING] correlated slow-loop snapshots start at >");
    print_ms_value(larger(SLOW_CYCLE_FLOOR_US,snapshot[CycleExec].baseline*12UL));
    Serial.println("; component timings may be nested, not additive");
}

static void print_summary(){
    Metric snapshot[MetricCount]{};
    uint32_t suppressed=0,replaced=0,queue_drops=0;
    uint32_t slow_count=0,slow_supp=0,slow_repl=0,slow_worst=0,slow_age=0;
    portENTER_CRITICAL(&timing_mux);
    for(uint8_t i=0;i<MetricCount;++i){
        snapshot[i]=metrics[i];
        metrics[i].window_sum=0;
        metrics[i].window_count=0;
        metrics[i].window_max=0;
        metrics[i].excessive=0;
    }
    suppressed=suppressed_events;suppressed_events=0;
    replaced=replaced_events;replaced_events=0;
    queue_drops=touch_queue_drops_window;touch_queue_drops_window=0;
    slow_count=slow_cycles_window;slow_cycles_window=0;
    slow_supp=slow_suppressed;slow_suppressed=0;
    slow_repl=slow_replaced;slow_replaced=0;
    slow_worst=slow_worst_us;slow_worst_us=0;
    slow_age=slow_queue_age_worst_ms;slow_queue_age_worst_ms=0;
    portEXIT_CRITICAL(&timing_mux);

    Serial.print("[T5-TIMING] 60s: touch avg=");
    print_ms_value(window_avg_us(snapshot[TouchGap]));
    Serial.print(" worst=");print_ms_value(snapshot[TouchGap].window_max);
    Serial.printf(" excessive=%lu",(unsigned long)snapshot[TouchGap].excessive);
    Serial.print(" | touch-exec worst=");print_ms_value(snapshot[TouchExec].window_max);
    Serial.print(" main=");print_ms_value(snapshot[MainGap].window_max);
    Serial.print(" mesh=");print_ms_value(snapshot[MeshExec].window_max);
    Serial.print(" ui=");print_ms_value(snapshot[UiExec].window_max);
    Serial.print(" display=");print_ms_value(snapshot[DisplayExec].window_max);
    Serial.printf(" queue-drops=%lu slow=%lu",(unsigned long)queue_drops,(unsigned long)slow_count);
    if(slow_count){
        Serial.print(" slow-worst=");print_ms_value(slow_worst);
        Serial.printf(" queue-age-worst=%lums",(unsigned long)slow_age);
    }
    Serial.printf(" suppressed=%lu/%lu replaced=%lu/%lu\n",
        (unsigned long)suppressed,(unsigned long)slow_supp,
        (unsigned long)replaced,(unsigned long)slow_repl);
}

static void print_slow(const SlowCycleEvent& slow){
    const CycleDetail& d=slow.detail;
    Serial.print("[T5-TIMING] SLOW cycle=");
    print_ms_value(slow.total_us);
    Serial.printf(" screen=%s action=%s keyboard=%d land=%d standby=%d cpu=%uMHz heap=%lu psram=%lu",
        d.screen,action_name(d.trigger),d.keyboard,d.landscape,d.standby,
        (unsigned)slow.cpu_mhz,(unsigned long)slow.heap,(unsigned long)slow.psram);
    Serial.print(" mesh=");print_ms_value(d.mesh_us);
    Serial.print(" ui=");print_ms_value(d.ui_us);
    Serial.print(" draw-max=");print_ms_value(d.draw_us);
    Serial.print(" display-sum=");print_ms_value(d.display_us);
    Serial.printf(" refreshes=%u mode=%u>%u",(unsigned)d.display_count,
        (unsigned)d.requested_mode,(unsigned)d.actual_mode);
    Serial.print(" input-sum=");print_ms_value(d.input_us);
    Serial.printf(" events=%u queue-age=%lums queue-depth=%u",
        (unsigned)d.input_events,(unsigned long)d.max_queue_age_ms,
        (unsigned)d.max_queue_depth);
    Serial.print(" status=");print_ms_value(d.status_us);
    Serial.printf(" text-wait=%lums",(unsigned long)d.text_wait_ms);
    Serial.println();
}
} // namespace

void t5_timing_begin(){
    portENTER_CRITICAL(&timing_mux);
    for(uint8_t i=0;i<MetricCount;++i)metrics[i]=Metric{};
    pending_event=Event{};pending_slow=SlowCycleEvent{};current_cycle=CycleDetail{};
    event_pending=false;
    suppressed_events=0;replaced_events=0;touch_queue_drops=0;touch_queue_drops_window=0;
    slow_cycles_window=0;slow_suppressed=0;slow_replaced=0;slow_worst_us=0;slow_queue_age_worst_ms=0;
    started_ms=millis();last_summary_ms=started_ms;last_serial_event_ms=0;last_slow_event_ms=0;
    last_touch_start_us=0;last_cycle_start_us=0;
    learning=true;
    current_section=T5TimingSection::Idle;current_section_started_us=micros();
    portEXIT_CRITICAL(&timing_mux);
    Serial.println("[T5-TIMING] learning normal timing for 30s; stall reports suppressed during learning");
}

void t5_timing_touch_reset(){
    portENTER_CRITICAL(&timing_mux);
    last_touch_start_us=0;
    portEXIT_CRITICAL(&timing_mux);
}

uint32_t t5_timing_touch_begin(){
    const uint32_t now=micros();
    uint32_t previous=0;
    portENTER_CRITICAL(&timing_mux);
    previous=last_touch_start_us;
    last_touch_start_us=now;
    portEXIT_CRITICAL(&timing_mux);
    if(previous)record_metric(TouchGap,(uint32_t)(now-previous));
    return now;
}

void t5_timing_touch_end(uint32_t started_us){
    record_metric(TouchExec,(uint32_t)(micros()-started_us));
}

void t5_timing_note_touch_queue_drop(){
    portENTER_CRITICAL(&timing_mux);
    touch_queue_drops++;
    touch_queue_drops_window++;
    portEXIT_CRITICAL(&timing_mux);
}

uint32_t t5_timing_cycle_begin(){
    const uint32_t now=micros();
    uint32_t previous=0;
    portENTER_CRITICAL(&timing_mux);
    previous=last_cycle_start_us;
    last_cycle_start_us=now;
    current_cycle=CycleDetail{};
    portEXIT_CRITICAL(&timing_mux);
    if(previous)record_metric(MainGap,(uint32_t)(now-previous));
    return now;
}

void t5_timing_cycle_end(uint32_t started_us){
    const uint32_t elapsed=(uint32_t)(micros()-started_us);
    record_metric(CycleExec,elapsed);
    maybe_queue_slow_cycle(elapsed);
}

uint32_t t5_timing_section_begin(T5TimingSection section){
    set_section(section);
    return micros();
}

void t5_timing_section_end(T5TimingSection section,uint32_t started_us){
    const uint32_t elapsed=(uint32_t)(micros()-started_us);
    if(section==T5TimingSection::Mesh)record_metric(MeshExec,elapsed);
    else if(section==T5TimingSection::Ui)record_metric(UiExec,elapsed);
    portENTER_CRITICAL(&timing_mux);
    if(section==T5TimingSection::Mesh)current_cycle.mesh_us=elapsed;
    else if(section==T5TimingSection::Ui)current_cycle.ui_us=elapsed;
    portEXIT_CRITICAL(&timing_mux);
    set_section(T5TimingSection::Idle);
}

uint32_t t5_timing_display_begin(){
    set_section(T5TimingSection::Display);
    return micros();
}

void t5_timing_display_end(uint32_t started_us){
    const uint32_t elapsed=(uint32_t)(micros()-started_us);
    record_metric(DisplayExec,elapsed);
    portENTER_CRITICAL(&timing_mux);
    current_cycle.display_us+=elapsed;
    if(current_cycle.display_count<255)current_cycle.display_count++;
    portEXIT_CRITICAL(&timing_mux);
    set_section(T5TimingSection::Ui);
}

void t5_timing_set_ui_context(const char* screen,bool keyboard,bool landscape,bool standby){
    portENTER_CRITICAL(&timing_mux);
    if(screen&&screen[0]){
        strncpy(current_cycle.screen,screen,sizeof(current_cycle.screen)-1);
        current_cycle.screen[sizeof(current_cycle.screen)-1]=0;
    }
    current_cycle.keyboard=keyboard;
    current_cycle.landscape=landscape;
    current_cycle.standby=standby;
    portEXIT_CRITICAL(&timing_mux);
}

void t5_timing_set_ui_action(T5UiAction action){
    portENTER_CRITICAL(&timing_mux);
    if(action!=T5UiAction::None)current_cycle.trigger=action;
    portEXIT_CRITICAL(&timing_mux);
}

void t5_timing_note_ui_draw(uint32_t elapsed_us){
    portENTER_CRITICAL(&timing_mux);
    if(elapsed_us>current_cycle.draw_us)current_cycle.draw_us=elapsed_us;
    portEXIT_CRITICAL(&timing_mux);
}

void t5_timing_note_ui_status(uint32_t elapsed_us){
    portENTER_CRITICAL(&timing_mux);
    current_cycle.status_us+=elapsed_us;
    portEXIT_CRITICAL(&timing_mux);
}

void t5_timing_note_text_wait(uint32_t wait_ms){
    portENTER_CRITICAL(&timing_mux);
    if(wait_ms>current_cycle.text_wait_ms)current_cycle.text_wait_ms=wait_ms;
    portEXIT_CRITICAL(&timing_mux);
}

void t5_timing_note_ui_input(uint32_t elapsed_us,uint32_t age_ms,uint32_t queue_depth){
    portENTER_CRITICAL(&timing_mux);
    current_cycle.input_us+=elapsed_us;
    if(current_cycle.input_events<65535)current_cycle.input_events++;
    if(age_ms>current_cycle.max_queue_age_ms)current_cycle.max_queue_age_ms=age_ms;
    if(queue_depth>current_cycle.max_queue_depth)
        current_cycle.max_queue_depth=(uint8_t)min((uint32_t)255,queue_depth);
    if(current_cycle.trigger==T5UiAction::None)current_cycle.trigger=T5UiAction::Touch;
    portEXIT_CRITICAL(&timing_mux);
}

void t5_timing_note_refresh(uint8_t requested_mode,uint8_t actual_mode){
    portENTER_CRITICAL(&timing_mux);
    current_cycle.requested_mode=requested_mode;
    current_cycle.actual_mode=actual_mode;
    portEXIT_CRITICAL(&timing_mux);
}

void t5_timing_service(){
    const uint32_t now=millis();
    if(learning&&now-started_ms>=LEARN_MS)finish_learning();
    if(learning)return;

    SlowCycleEvent slow{};
    Event event{};
    bool have_slow=false,have_event=false;
    if(now-last_serial_event_ms>=SERIAL_EVENT_SPACING_MS){
        portENTER_CRITICAL(&timing_mux);
        if(pending_slow.valid){
            slow=pending_slow;
            pending_slow=SlowCycleEvent{};
            have_slow=true;
        }else if(event_pending){
            event=pending_event;
            event_pending=false;
            have_event=true;
        }
        portEXIT_CRITICAL(&timing_mux);
    }
    if(have_slow){
        last_serial_event_ms=now;
        print_slow(slow);
    }else if(have_event){
        last_serial_event_ms=now;
        Serial.printf("[T5-TIMING] EXCESS %s observed=",metric_name(event.metric));
        print_ms_value(event.observed_us);
        Serial.print(" normal=");print_ms_value(event.normal_us);
        Serial.print(" over-normal=");print_ms_value(event.observed_us>event.normal_us?event.observed_us-event.normal_us:0);
        Serial.print(" limit=");print_ms_value(event.limit_us);
        Serial.printf(" main-section=%s",section_name(event.section));
        if(event.section!=T5TimingSection::Idle){
            Serial.print(" section-age=");
            print_ms_value(event.section_age_us);
        }
        Serial.println();
    }

    if(now-last_summary_ms>=SUMMARY_MS){
        last_summary_ms=now;
        print_summary();
    }
}
#endif
