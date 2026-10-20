#include "../src/setup_short_name.h"
#include <cassert>
#include <cstring>

static void check(const char* long_name, const char* expected) {
    char short_name[5]{};
    meshink_suggest_short_name(long_name,short_name);
    assert(std::strcmp(short_name,expected)==0);
}
int main() {
    check("samo-T5paper","ST5P");
    check("samo-Techo","STEC");
    check("samo-Tdeck","STDE");
    check("samo-Heltec","SHEL");
    check("MeshInk","MINK");
    check("MyTracker","MKER");
    check("TDeck","TECK");
    check("X","X");
    check("AB","AB");
    check("samo_T5paper","ST5P");
    check("samo T5paper","ST5P");
    check("samo-T","ST");
    check("", "");
    check("---", "");
    return 0;
}
