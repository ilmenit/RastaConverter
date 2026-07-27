#pragma once

// Stopping a run from outside the program.
//
// A conversion is hours of work held in memory, and until now Ctrl+C or a kill
// threw all of it away: the process died between autosaves with nothing written
// for the time since the last one. These make an interrupt mean what the Stop
// button means - finish the current evaluation, save, exit.
//
// The handler does the only thing a signal handler may safely do, which is set
// a flag; every loop that can run for a long time asks about it.

namespace interrupts {

// Installs handlers for SIGINT and SIGTERM (and SIGHUP where it exists).
// Call once, before SDL initialises, so SDL leaves the signals alone.
void InstallInterruptHandlers();

// True once an interrupt has arrived. Never resets: one Ctrl+C is a decision.
bool StopRequested();

} // namespace interrupts
