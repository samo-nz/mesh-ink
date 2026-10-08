#include "device_settings_snapshot.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stddef.h>
#include <string.h>

namespace meshink_device_settings {
namespace {
enum class Type:uint8_t{Byte,Boolean,Short,SignedInt,String};
struct Field {const char* key;Type type;uint16_t offset;uint16_t capacity;};
#define F(key,type,member) {key,Type::type,offsetof(Snapshot,member),sizeof(((Snapshot*)0)->member)}
constexpr Field UI[]={
    F("light_mode",Byte,light_mode),
    F("light_timeout",Byte,light_timeout),
    F("light_level",Byte,light_level),
    F("standby_timeout",Byte,standby_timeout),
    F("deep_standby",Boolean,deep_standby),
    F("night_start",Short,night_start),
    F("night_end",Short,night_end),
    F("timezone",Byte,timezone),
    F("tz_v2",Boolean,tz_v2),
    F("custom_tz_min",SignedInt,custom_tz_min),
    F("auto_tz_label",String,auto_tz_label),
    F("auto_tz_rule",String,auto_tz_rule),
    F("map_imperial",Boolean,map_imperial)
};
constexpr Field GPS[]={
    F("constellation",Byte,gps_constellation),
    F("mode_v2",Boolean,gps_mode_v2),
    F("ds_power_save",Boolean,gps_deep_sleep)
};
constexpr Field RTC[]={
    F("source",Byte,rtc_source),
    F("mode",Byte,rtc_mode)
};
#undef F
constexpr size_t UI_COUNT=sizeof(UI)/sizeof(UI[0]);
constexpr size_t GPS_COUNT=sizeof(GPS)/sizeof(GPS[0]);
constexpr size_t RTC_COUNT=sizeof(RTC)/sizeof(RTC[0]);
static_assert(UI_COUNT+GPS_COUNT+RTC_COUNT<=32,"Device settings presence mask exhausted");

bool read_group(const char* name,const Field* fields,size_t count,uint8_t first,Snapshot& out){
    Preferences prefs;
    if(!prefs.begin(name,true))return true; // absent namespace means defaults
    uint8_t* raw=(uint8_t*)&out;
    bool ok=true;
    for(size_t i=0;i<count&&ok;++i){
        const Field& f=fields[i];
        if(!prefs.isKey(f.key))continue;
        out.present|=(uint32_t)1U<<(first+i);
        uint8_t* field=raw+f.offset;
        switch(f.type){
            case Type::Byte:field[0]=prefs.getUChar(f.key,0);break;
            case Type::Boolean:field[0]=prefs.getBool(f.key,false)?1:0;break;
            case Type::Short:{
                const uint16_t n=prefs.getUShort(f.key,0);
                memcpy(field,&n,2);break;
            }
            case Type::SignedInt:{
                const int32_t n=prefs.getInt(f.key,0);
                memcpy(field,&n,4);break;
            }
            case Type::String:{
                const String value=prefs.getString(f.key,"");
                if(value.length()>=f.capacity){ok=false;break;}
                memcpy(field,value.c_str(),value.length()+1);
                break;
            }
        }
    }
    prefs.end();
    return ok;
}
bool write_group(const char* name,const Field* fields,size_t count,uint8_t first,const Snapshot& value){
    Preferences prefs;
    if(!prefs.begin(name,false))return false;
    const uint8_t* raw=(const uint8_t*)&value;
    bool ok=true;
    for(size_t i=0;i<count;++i){
        const Field& f=fields[i];
        if(!(value.present&((uint32_t)1U<<(first+i)))){
            if(prefs.isKey(f.key))ok=prefs.remove(f.key)&&ok;
            continue;
        }
        const uint8_t* field=raw+f.offset;
        bool wrote=false;
        switch(f.type){
            case Type::Byte:wrote=prefs.putUChar(f.key,field[0])==1;break;
            case Type::Boolean:wrote=prefs.putBool(f.key,field[0]!=0)==1;break;
            case Type::Short:{
                uint16_t n=0;memcpy(&n,field,2);
                wrote=prefs.putUShort(f.key,n)==2;break;
            }
            case Type::SignedInt:{
                int32_t n=0;memcpy(&n,field,4);
                wrote=prefs.putInt(f.key,n)==4;break;
            }
            case Type::String:{
                const size_t length=strnlen((const char*)field,f.capacity);
                wrote=prefs.putString(f.key,(const char*)field)==length;
                break;
            }
        }
        ok=wrote&&ok;
    }
    prefs.end();
    return ok;
}
bool boolean_valid(uint8_t n){return n<=1;}
} // namespace

bool valid(const Snapshot& s){
    if(s.magic!=MAGIC||s.version!=VERSION||s.bytes!=sizeof(Snapshot)||
       (s.present>> (UI_COUNT+GPS_COUNT+RTC_COUNT))!=0)return false;
    if(s.light_mode>2||s.light_timeout>4||s.light_level>100||
       s.standby_timeout>3||s.night_start>=1440||s.night_end>=1440||
       s.timezone>8||s.custom_tz_min< -720||s.custom_tz_min>840)return false;
    if(!boolean_valid(s.deep_standby)||!boolean_valid(s.tz_v2)||
       !boolean_valid(s.map_imperial)||!boolean_valid(s.gps_mode_v2)||
       !boolean_valid(s.gps_deep_sleep))return false;
    if(!memchr(s.auto_tz_label,0,sizeof(s.auto_tz_label))||
       !memchr(s.auto_tz_rule,0,sizeof(s.auto_tz_rule)))return false;
    return true;
}
bool capture(Snapshot& out){
    out=Snapshot{};
    out.magic=MAGIC;out.version=VERSION;out.bytes=sizeof(Snapshot);
    if(!read_group("t5-ui",UI,UI_COUNT,0,out)||
       !read_group("t5-gnss",GPS,GPS_COUNT,UI_COUNT,out)||
       !read_group("t5-rtc",RTC,RTC_COUNT,UI_COUNT+GPS_COUNT,out))
        return false;
    return valid(out);
}
bool apply(const Snapshot& value){
    if(!valid(value))return false;
    // Only whitelisted settings are modified; other keys remain untouched.
    return write_group("t5-ui",UI,UI_COUNT,0,value)&&
           write_group("t5-gnss",GPS,GPS_COUNT,UI_COUNT,value)&&
           write_group("t5-rtc",RTC,RTC_COUNT,UI_COUNT+GPS_COUNT,value);
}
} // namespace meshink_device_settings
