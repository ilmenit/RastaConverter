#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Wincon.h>
#include <DbgHelp.h>
#include <csignal>
#include <iostream>

static BOOL WINAPI RcConsoleCtrlHandler(DWORD ctrl_type)
{
    (void)ctrl_type;
    return FALSE; // allow normal processing
}

void RegisterConsoleCtrlLogger()
{
    SetConsoleCtrlHandler(RcConsoleCtrlHandler, TRUE);
}

static LONG WINAPI RcUnhandledExceptionFilter(EXCEPTION_POINTERS* ep)
{
    if (ep && ep->ExceptionRecord) {
        std::cerr << "[SEH] Unhandled exception code=0x" << std::hex
                  << ep->ExceptionRecord->ExceptionCode << std::dec
                  << " addr=" << ep->ExceptionRecord->ExceptionAddress
                  << std::endl;

        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        CONTEXT* ctx = ep->ContextRecord;
        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        SymInitialize(process, NULL, TRUE);
        SymRefreshModuleList(process);

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
        for (int i = 0; i < 32; ++i) {
            if (!StackWalk64(machine, process, thread, &frame, ctx, NULL,
                             SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                break;
            DWORD64 addr = frame.AddrPC.Offset;
            if (!addr) break;
            char buffer[sizeof(SYMBOL_INFO) + 512] = {0};
            PSYMBOL_INFO sym = reinterpret_cast<PSYMBOL_INFO>(buffer);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 511;
            DWORD64 disp = 0;
            if (SymFromAddr(process, addr, &disp, sym)) {
                std::cerr << "  [" << i << "] 0x" << std::hex << addr << std::dec
                          << " " << sym->Name << "+0x" << std::hex << disp << std::dec << std::endl;
            } else {
                std::cerr << "  [" << i << "] 0x" << std::hex << addr << std::dec << std::endl;
            }
        }
    } else {
        std::cerr << "[SEH] Unhandled exception (no details)" << std::endl;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void RegisterUnhandledExceptionLogger()
{
    SetUnhandledExceptionFilter(RcUnhandledExceptionFilter);
}

static void RcSignalHandler(int sig)
{
    std::cerr << "[SIG] signal=" << sig << std::endl;
}

void RegisterSignalHandlers()
{
    std::signal(SIGABRT, RcSignalHandler);
    std::signal(SIGSEGV, RcSignalHandler);
}

#endif




