// SPDX-License-Identifier: MIT
// /usr/lib/libScreenReader.dylib: Terminal.app names it as a dependency and imports no symbol
// from it (VoiceOver's client library). There is no screen reader on this system.
int SFScreenReaderIsAbsent = 1;
