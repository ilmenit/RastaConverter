#pragma once

#ifdef _WIN32
// Only declarations; implemented in ConsoleCtrlWin.cpp to avoid pulling Windows headers here
void RegisterConsoleCtrlLogger();
void RegisterUnhandledExceptionLogger();
void RegisterSignalHandlers();
#else
inline void RegisterConsoleCtrlLogger() {}
inline void RegisterUnhandledExceptionLogger() {}
inline void RegisterSignalHandlers() {}
#endif


