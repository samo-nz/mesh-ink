#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// Strict basename validation: filename may never escape SD root. Both archive
// naming conventions are accepted; their content formats remain unchanged.
namespace meshink_backup_filename {
constexpr size_t MAX_LENGTH=63;
inline const char* prefix(uint8_t protocol){
    return protocol==1?"meshink-core-":protocol==2?"meshink-tastic-":nullptr;
}
inline bool decimal(const char* p,size_t n){
    for(size_t i=0;i<n;++i)if(p[i]<'0'||p[i]>'9')return false;
    return true;
}
inline bool valid(uint8_t protocol,const char* name){
    if(!name||!prefix(protocol))return false;
    const size_t n=strnlen(name,MAX_LENGTH+1);
    if(n>MAX_LENGTH)return false;
    if(n==12){
        const char* old=protocol==1?"MCBK":"MTBK";
        return !strncmp(name,old,4)&&decimal(name+4,4)&&!strcmp(name+8,".BAK");
    }
    const char* pfx=prefix(protocol);
    const size_t prelen=strlen(pfx);
    if(n<prelen+20||strncmp(name,pfx,prelen))return false;
    const char* s=name+prelen;
    if(!decimal(s,4)||s[4]!='-'||!decimal(s+5,2)||s[7]!='-'||
       !decimal(s+8,2)||s[10]!='-'||!decimal(s+11,2)||
       s[13]!='-'||!decimal(s+14,2))return false;
    const unsigned y=(unsigned)(s[0]-'0')*1000+(unsigned)(s[1]-'0')*100+
        (unsigned)(s[2]-'0')*10+(unsigned)(s[3]-'0');
    const unsigned mon=(unsigned)(s[5]-'0')*10+(unsigned)(s[6]-'0');
    const unsigned day=(unsigned)(s[8]-'0')*10+(unsigned)(s[9]-'0');
    const unsigned hour=(unsigned)(s[11]-'0')*10+(unsigned)(s[12]-'0');
    const unsigned minute=(unsigned)(s[14]-'0')*10+(unsigned)(s[15]-'0');
    if(y<2020||y>2099||mon<1||mon>12||day<1||day>31||hour>23||minute>59)
        return false;
    if(!strcmp(s+16,".BAK"))return true;
    return s[16]=='-'&&decimal(s+17,2)&&
           !(s[17]=='0'&&s[18]=='0')&&!strcmp(s+19,".BAK");
}
// Sort newest first, including same-minute -01/-02 revisions. Timestamped
// files sort before legacy archives; legacy numbering sorts numerically.
inline int compare(uint8_t protocol,const char* a,const char* b){
    if(!valid(protocol,a)||!valid(protocol,b))return 0;
    const size_t prelen=strlen(prefix(protocol));
    const bool a_time=!strncmp(a,prefix(protocol),prelen);
    const bool b_time=!strncmp(b,prefix(protocol),prelen);
    if(a_time!=b_time)return a_time?1:-1;
    if(!a_time)return strcmp(a,b);
    const int minute= strncmp(a+prelen,b+prelen,16);
    if(minute)return minute;
    const char* as=a+prelen+16;
    const char* bs=b+prelen+16;
    const int ai=*as=='-'?(as[1]-'0')*10+(as[2]-'0'):0;
    const int bi=*bs=='-'?(bs[1]-'0')*10+(bs[2]-'0'):0;
    return ai-bi;
}
inline bool make_timestamped(uint8_t protocol,int year,int month,int day,
                             int hour,int minute,char* output,size_t capacity){
    if(!output||!capacity||!prefix(protocol))return false;
    const int n=snprintf(output,capacity,"%s%04d-%02d-%02d-%02d-%02d.BAK",
         prefix(protocol),year,month,day,hour,minute);
    if(n<0||(size_t)n>=capacity){output[0]=0;return false;}
    if(!valid(protocol,output)){output[0]=0;return false;}
    return true;
}
inline bool with_suffix(uint8_t protocol,const char* base,unsigned suffix,
                        char* output,size_t capacity){
    if(!valid(protocol,base)||!output||capacity<5||suffix>99)return false;
    if(suffix==0){
        const size_t len=strlen(base);
        if(len>=capacity)return false;
        memcpy(output,base,len+1);return true;
    }
    const size_t len=strlen(base);
    if(len<4||strcmp(base+len-4,".BAK"))return false;
    const int n=snprintf(output,capacity,"%.*s-%02u.BAK",
        (int)(len-4),base,suffix);
    return n>=0&&(size_t)n<capacity&&valid(protocol,output);
}
}
