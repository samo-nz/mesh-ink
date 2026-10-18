#include "backup_filename.h"
#include <cassert>
#include <cstring>
using namespace meshink_backup_filename;
int main(){
    assert(valid(1,"MCBK0005.BAK"));
    assert(valid(2,"MTBK0004.BAK"));
    assert(!valid(1,"MTBK0004.BAK"));
    assert(!valid(2,"MCBK0005.BAK"));
    assert(valid(1,"meshink-core-2026-10-18-22-40.BAK"));
    assert(valid(2,"meshink-tastic-2026-10-18-22-40.BAK"));
    assert(!valid(1,"meshink-tastic-2026-10-18-22-40.BAK"));
    assert(!valid(2,"meshink-tastic-2026-00-18-22-40.BAK"));
    assert(!valid(2,"meshink-tastic-2026-10-18-24-40.BAK"));
    assert(!valid(2,"meshink-tastic-2026-10-18-22-60.BAK"));
    assert(!valid(2,"../MTBK0004.BAK"));
    assert(!valid(2,"meshink-tastic-2026-10-18-22-40/other.BAK"));
    char out[64]{};
    assert(make_timestamped(2,2026,10,18,22,40,out,sizeof(out)));
    assert(!strcmp(out,"meshink-tastic-2026-10-18-22-40.BAK"));
    char next[64]{};
    assert(with_suffix(2,out,1,next,sizeof(next)));
    assert(!strcmp(next,"meshink-tastic-2026-10-18-22-40-01.BAK"));
    assert(valid(2,next));
    assert(with_suffix(2,out,99,next,sizeof(next)));
    assert(!strcmp(next,"meshink-tastic-2026-10-18-22-40-99.BAK"));
    assert(!with_suffix(2,out,100,next,sizeof(next)));
    assert(!make_timestamped(2,2026,10,18,22,40,out,12));
    const char* a="meshink-tastic-2026-10-18-22-40.BAK";
    const char* b="meshink-tastic-2026-10-18-22-40-01.BAK";
    const char* c="meshink-tastic-2026-10-18-22-40-02.BAK";
    const char* d="meshink-tastic-2026-10-18-22-41.BAK";
    assert(compare(2,b,a)>0); // suffix archive newer than original
    assert(compare(2,c,b)>0);
    assert(compare(2,d,c)>0); // newer minute wins over any suffix
    assert(compare(2,a,d)<0);
    assert(compare(2,a,"MTBK0004.BAK")>0);
    assert(compare(1,"MCBK0010.BAK","MCBK0009.BAK")>0);
    // Simulate an SD directory enumerating an old backup first. The bounded
    // UI shortlist must still retain the newest filename when it fills up.
    const char* files[3]={"MTBK0001.BAK",a,c};
    const char* shortlist[2]{};
    size_t count=0;
    for(const char* item:files){
        if(count==2&&compare(2,item,shortlist[count-1])<=0)continue;
        size_t insert=count<2?count++:count-1;
        while(insert&&compare(2,item,shortlist[insert-1])>0){
            shortlist[insert]=shortlist[insert-1];--insert;
        }
        shortlist[insert]=item;
    }
    assert(!strcmp(shortlist[0],c));
    assert(!strcmp(shortlist[1],a));
    return 0;
}
