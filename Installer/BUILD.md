# TDSpeed Preview build and packaging

The maintained instructions are in [docs/BUILD.md](../docs/BUILD.md).

Use the Windows Release/AddressSanitizer CMake presets. The standalone and VST3 link the same processor/editor library. All engine sources are vendored. Tools/Build-Standalone.ps1 builds and tests both deliverables, creates the installer, portable/VST3/source archives, and verifies source hashes and the original signed standard VB-CABLE package. BuildDir, DependencyDir and ReleaseDir can be specified explicitly. Microsoft runtime DLLs are located through the installed Visual Studio toolchain rather than a hard-coded version.

RC settings and recovery files live under %APPDATA%\TDSpeed Preview. RC startup registration, installation and VST3 identities are independent of older builds. Retain the root licence, dependency notices, corresponding source and visible VB-Audio donation/distribution information in every redistributed package.

Hardware diagnostics and publication acceptance gates are documented in the main build guide and the accompanying validation report. CI cannot substitute for physical device tests, a fresh-machine installation or listening acceptance.
