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
    return 0;
}
