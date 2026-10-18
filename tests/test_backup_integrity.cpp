#include "backup_integrity.h"
#include <cassert>
using namespace meshink_backup_integrity;
int main(){
    Journal mc{MESSAGE_MAGIC,3,MESSAGE_CAPACITY,0,2,8};
    constexpr size_t core_bytes=JOURNAL_HEADER_BYTES+MESSAGE_CAPACITY*CORE_RECORD_BYTES;
    constexpr size_t mt_bytes=JOURNAL_HEADER_BYTES+MESSAGE_CAPACITY*MT_RECORD_BYTES;
    assert(valid_journal(mc,core_bytes,1));
    assert(!valid_journal(mc,core_bytes,2));
    assert(!valid_journal(mc,core_bytes-1,1));
    mc.count=251;assert(!valid_journal(mc,core_bytes,1));
    mc.count=2;mc.head=250;assert(!valid_journal(mc,core_bytes,1));
    Journal mt{MESSAGE_MAGIC,4,MESSAGE_CAPACITY,249,250,900};
    assert(valid_journal(mt,mt_bytes,2));
    assert(!valid_journal(mt,core_bytes,2));
    assert(!valid_journal(mt,mt_bytes,1));
    Node n{NODE_MAGIC,1,8,3,0};
    uint8_t rec[NODE_RECORD_BYTES]{};rec[0]=6;
    uint32_t sum=node_seed(n.generation,n.count);
    for(unsigned i=0;i<n.count;++i)sum=fnv(sum,rec,sizeof(rec));
    n.checksum=sum;
    assert(valid_node_header(n,NODE_HEADER_BYTES+3*NODE_RECORD_BYTES));
    assert(!valid_node_header(n,NODE_HEADER_BYTES+2*NODE_RECORD_BYTES));
    n.count=65;assert(!valid_node_header(n,NODE_HEADER_BYTES+65*NODE_RECORD_BYTES));
    return 0;
}
