#include "Interrupt.h"

#include <atomic>
#include <csignal>

namespace interrupts {
namespace {

// Written from a signal handler and read from the thread coordinating the
// workers, so it is atomic rather than a plain bool.
std::atomic<bool> g_stop_requested{false};

extern "C" void HandleInterrupt(int)
{
	g_stop_requested.store(true, std::memory_order_relaxed);
	// Deliberately no re-raise and no default disposition restored: a second
	// Ctrl+C should not kill a process that is in the middle of writing its
	// output files. A run that refuses to stop can still be killed with -9.
}

} // namespace

void InstallInterruptHandlers()
{
	std::signal(SIGINT, HandleInterrupt);
	std::signal(SIGTERM, HandleInterrupt);
#if defined(SIGHUP)
	// Closing the terminal a conversion was started from is a stop, not a
	// reason to lose the work.
	std::signal(SIGHUP, HandleInterrupt);
#endif
}

bool StopRequested()
{
	return g_stop_requested.load(std::memory_order_relaxed);
}

} // namespace interrupts
