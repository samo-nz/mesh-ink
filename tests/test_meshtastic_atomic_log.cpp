#include "protocol/meshtastic_atomic_log.h"
#include <cassert>
#include <cstring>
using meshink_atomic_log::compose;
int main(){
    char line[meshink_atomic_log::CAPACITY]{};
    const char* body="[NodeInfo] Send our nodeinfo to mesh\n";
    size_t n=compose(line,sizeof(line),"INFO ",23*3600+52*60+4,10,"Radio",body,strlen(body));
    assert(n==strlen(line));
    assert(!strcmp(line,"INFO  | 23:52:04 10 [NodeInfo] Send our nodeinfo to mesh\n"));
    n=compose(line,sizeof(line),"DEBUG",0,9,"MeshService","Hello\n",6);
    assert(!strcmp(line,"DEBUG | ??:??:?? 9 [MeshService] Hello\n"));
    assert(n==strlen(line));
    // Mixed threads must never cause a line to be emitted as separate prefixes
    // and bodies; even a clipped record is a complete newline-terminated line.
    char small[48]{};
    const char* long_message="abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\n";
    n=compose(small,sizeof(small),"WARN ",0,123,"worker",
              long_message,strlen(long_message));
    assert(n<=sizeof(small)-1);
    assert(small[n-1]=='\n');
    assert(!strchr(small,'\033'));
    n=compose(line,sizeof(line),"ERROR",0,0,nullptr,"Bare message\n",13);
    assert(!strcmp(line,"ERROR | ??:??:?? 0 Bare message\n"));
    return 0;
}
