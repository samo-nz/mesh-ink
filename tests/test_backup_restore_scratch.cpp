#include "backup_restore_scratch.h"
#include <cassert>
#include <cstring>
using namespace meshink_restore_scratch;
int main(){
    char path[24]{};
    for(unsigned i=0;i<SLOTS;++i)
        for(unsigned legacy=0;legacy<2;++legacy)
            for(unsigned old=0;old<2;++old){
                assert(make_path(path,sizeof(path),i,legacy,old));
                assert(path[0]=='/'&&strlen(path)==10);
                assert(!strstr(path,".."));
            }
    assert(make_path(path,sizeof(path),0,true,true));
    assert(!strcmp(path,"/rst00.old"));
    assert(make_path(path,sizeof(path),59,true,false));
    assert(!strcmp(path,"/rst59.tmp"));
    assert(make_path(path,sizeof(path),0,false,true));
    assert(!strcmp(path,"/r2s00.old"));
    assert(make_path(path,sizeof(path),59,false,false));
    assert(!strcmp(path,"/r2s59.tmp"));
    assert(!make_path(path,sizeof(path),SLOTS,true,true));
    assert(!make_path(path,4,0,false,true));
    assert(!make_path(nullptr,0,0,false,true));
}
