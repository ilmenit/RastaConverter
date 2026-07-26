# Bundled MADS assembler

Use `build.sh` on macOS/Linux and `build.bat` on Windows. The `mads`
launcher selects the bundled executable for the current host:

- `mads-macos-arm64` — MADS 2.1.8, native Apple Silicon build.
- `mads-linux-x86_64` — retained Linux x86-64 build.
- `mads.exe` — retained 32-bit x86 Windows build; runs on supported
  32-bit and 64-bit Windows environments.

The macOS binary was built from official Mad-Assembler commit
`7cccd6c65154a3c199eb504e1634c4ae788b04f0` with Free Pascal 3.2.2.
Its SHA-256 is
`22655b154b3642a4ed61fbe07ed3259e96cea0e583431cfd763cd2470d7f9caa`.

Unsupported host/architecture combinations fail with an explicit message
instead of attempting to execute a binary for another platform.
