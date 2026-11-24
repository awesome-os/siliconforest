# MediaPlayer local state facade

Adapted from TERM8's own MIT fixture in `runs/sf-term8/shim/MediaPlayer.m`,
which let VLC's Cocoa interface launch rather than dereferencing a missing
weakly imported Now Playing constant. Own public declarations, Linux/Nix
toolchain and SiliconForest open headers; no Apple SDK or Mac build.

There is no system Now Playing service, media-key routing or external command
source. A once-only diagnostic states that limitation. This provider keeps
local copied information, playback state and command settings. Registration
does not invoke a command or pretend to deliver an event. Target/action pairs
are nonretaining and deduplicated; opaque block tokens retain copied handlers
until removal. Artwork requests call the application's own handler.

The [public registration contract](https://developer.apple.com/documentation/mediaplayer/mpremotecommand/addtarget(handler:))
defines an opaque handler token; the
[removal contract](https://developer.apple.com/documentation/mediaplayer/mpremotecommand/removetarget(_:action:))
allows nil targets and selectors as wildcards. Only the VLC-used command
classes/settings are covered. Generic objects for other command accessors do
not implement every native command subclass or event type.

Build `build.nix`. `mediaplayer-probe` checks copied state, command identity,
nonretaining target removal, opaque block removal, absence of synthetic events,
skip intervals and local artwork callbacks. M1 prefix execution completed at
20:02:03–04Z: all local assertions pass with the explicit unavailable-service
diagnostic. The readonly metadata output matches native exactly, using the
same Linux-built binary SHA-256
`eb8738cceb4997b8ac80910bee1190c982c33f17b658c2e329909383b1faa710`.
Native ran in a fresh deleted temporary directory with remote SHA verification,
30-second limit and no DYLD overrides. The native reference uses only `--metadata`: constants
and class lookup, without setting the Mac's Now Playing information, registering
commands or invoking its real services. The full local-state test is M1-only.
No canonical install has occurred.
