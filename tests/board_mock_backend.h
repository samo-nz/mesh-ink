#pragma once

inline unsigned meshink_board_mock_companion_begins=0;
inline unsigned meshink_board_mock_local_begins=0;
inline unsigned meshink_board_mock_boot_completes=0;
inline unsigned meshink_board_mock_exit_feedback=0;
inline unsigned meshink_board_mock_release_resources=0;

inline const char* meshink_board_name(){return "MOCK-BOARD";}
inline bool meshink_board_has_gps(){return true;}
inline void meshink_board_begin_companion(){++meshink_board_mock_companion_begins;}
inline void meshink_board_begin_local(){++meshink_board_mock_local_begins;}
inline void meshink_board_boot_complete(){++meshink_board_mock_boot_completes;}
inline void meshink_board_companion_exit_feedback_begin(){++meshink_board_mock_exit_feedback;}
inline void meshink_board_companion_release_resources(){++meshink_board_mock_release_resources;}
