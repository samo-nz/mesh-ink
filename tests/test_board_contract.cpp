#define MESHINK_BOARD_BACKEND_HEADER "board_mock_backend.h"
#include "hardware/board.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main(){
    assert(std::strcmp(meshink_board_name(),"MOCK-BOARD")==0);
    assert(meshink_board_has_gps());
    meshink_board_begin_companion();
    meshink_board_begin_local();
    meshink_board_boot_complete();
    meshink_board_companion_exit_feedback_begin();
    meshink_board_companion_release_resources();
    assert(meshink_board_mock_companion_begins==1);
    assert(meshink_board_mock_local_begins==1);
    assert(meshink_board_mock_boot_completes==1);
    assert(meshink_board_mock_exit_feedback==1);
    assert(meshink_board_mock_release_resources==1);
    std::cout << "PASS: generic board lifecycle contract compiles with a non-T5 backend.\n";
    return 0;
}
