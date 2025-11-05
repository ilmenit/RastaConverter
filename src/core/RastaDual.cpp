// Dual-mode helper functions (display, data I/O, PMG)
// NOTE: MainLoopDual() is in dual/RastaDual_MainLoop.cpp
// NOTE: PrecomputeDualTables() is in dual/RastaDual_Tables.cpp
#include "rasta.h"
#include "Program.h"
#include "Evaluator.h"
#include "TargetPicture.h"
#include "debug_log.h"
#include <fstream>
#include <iomanip>

extern const char *program_version;
// Forward declaration provided in rasta.cpp
unsigned char ConvertColorRegisterToRawData(e_target t);

// Thin stub: function implementations are organized in src/core/dual/
// - Display:    src/core/dual/RastaDual_Display.cpp
// - Tables:     src/core/dual/RastaDual_Tables.cpp
// - Data I/O:   src/core/dual/RastaDual_DataIO.cpp
// - Main loop:  src/core/dual/RastaDual_MainLoop.cpp
// This file intentionally left minimal to avoid duplicate symbols.
