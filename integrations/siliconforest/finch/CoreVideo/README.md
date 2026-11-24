# CoreVideo (staged only)

`build.nix` builds all ten Finch CoreVideo implementation files. Own declarations,
PD CFRuntime, existing IOSurface/Mesa/CG providers. ICC and CoreMedia709 color
spaces are dynamically resolved and return NULL if the real provider is absent;
there is no substitute profile or success stub. GL/Metal cache limitations remain
those in Finch. CPU tests create neither GL contexts nor Metal/output devices.

Whole414 exports retains89/97 live PD names; missing8 are PD's private display-link
ObjC class/metaclass/ivars. `sf-displaylink.nix` is a separate reconciliation
candidate: exact PD display-link source from the package matching the live binary,
Finch's independent host-clock functions, and whole Finch buffer/format code.
It retains97/97 exports, but omits8 whole-Finch display-link additions. Do not pass
objects between the two display-link implementations. Both candidates are staged.

M1 CPU comparison and ownership checks exit0 for both. Whole-CV same-byte M5
comparison remains DIFF (CF descriptions, IOSurface allocation size, missing
color-space providers); the independent probe MATCH. Display/compositor regression
and decoder hardware execution remain integration gates for Claude.
