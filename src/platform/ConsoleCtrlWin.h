#pragma once

#ifdef _WIN32
// Isolate heavy Windows headers to avoid macro/type conflicts
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Wincon.h>
#include <iostream>
#include <csignal>
#include <dbghelp.h>

inline BOOL WINAPI RcConsoleCtrlHandler(DWORD ctrl_type)
{
    (void)ctrl_type; // silence unused variable warning when THREAD_DEBUG is off
#ifdef THREAD_DEBUG
    const char* name = "";
    switch (ctrl_type) {
    case CTRL_C_EVENT: name = "CTRL_C_EVENT"; break;
    case CTRL_BREAK_EVENT: name = "CTRL_BREAK_EVENT"; break;
    case CTRL_CLOSE_EVENT: name = "CTRL_CLOSE_EVENT"; break;
    case CTRL_LOGOFF_EVENT: name = "CTRL_LOGOFF_EVENT"; break;
    case CTRL_SHUTDOWN_EVENT: name = "CTRL_SHUTDOWN_EVENT"; break;
    default: name = "UNKNOWN_CTRL"; break;
    }
    std::cout << "[CTRL] " << name << std::endl;
#endif
    return FALSE; // allow normal processing
}

inline void RegisterConsoleCtrlLogger()
{
    SetConsoleCtrlHandler(RcConsoleCtrlHandler, TRUE);
}

inline LONG WINAPI RcUnhandledExceptionFilter(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord) {
        std::cerr << "[SEH] Unhandled exception code=0x" << std::hex
                  << ep->ExceptionRecord->ExceptionCode << std::dec
                  << " addr=" << ep->ExceptionRecord->ExceptionAddress
                  << std::endl;

        // Try to capture a minimal stack trace
        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        CONTEXT* ctx = ep->ContextRecord;
        SymInitialize(process, NULL, TRUE);

        STACKFRAME64 frame = {};
#ifdef _M_X64
        DWORD machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = ctx->Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx->Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx->Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
#else
        DWORD machine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = ctx->Eip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx->Ebp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx->Esp;
        frame.AddrStack.Mode = AddrModeFlat;
#endif
        std::cerr << "[SEH] Backtrace:" << std::endl;
        for (int i = 0; i < 16; ++i) {
            if (!StackWalk64(machine, process, thread, &frame, ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                break;
            DWORD64 addr = frame.AddrPC.Offset;
            if (!addr) break;
            char buffer[sizeof(SYMBOL_INFO) + 256] = {0};
            PSYMBOL_INFO sym = reinterpret_cast<PSYMBOL_INFO>(buffer);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 255;
            DWORD64 disp = 0;
            if (SymFromAddr(process, addr, &disp, sym)) {
                std::cerr << "  [" << i << "] 0x" << std::hex << addr << std::dec << " " << sym->Name << "+0x" << std::hex << disp << std::dec << std::endl;
            } else {
                std::cerr << "  [" << i << "] 0x" << std::hex << addr << std::dec << std::endl;
            }
        }
    } else {
        std::cerr << "[SEH] Unhandled exception (no details)" << std::endl;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

inline void RegisterUnhandledExceptionLogger()
{
    SetUnhandledExceptionFilter(RcUnhandledExceptionFilter);
}

inline void RcSignalHandler(int sig)
{
    std::cerr << "[SIG] signal=" << sig << std::endl;
}

inline void RegisterSignalHandlers()
{
    std::signal(SIGABRT, RcSignalHandler);
    std::signal(SIGSEGV, RcSignalHandler);
}

#else
inline void RegisterConsoleCtrlLogger() {}
inline void RegisterUnhandledExceptionLogger() {}
inline void RegisterSignalHandlers() {}
#endif


