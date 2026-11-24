drm/apple: SFDcp stage lines

Three lines for following a first bring-up on a console: each AFK endpoint start request,
dcp_start() with the firmware compat chosen, and the first occurrence of every IOMFB callback
the firmware makes with whether the driver has a handler (sf_dcp_note_callback(), in the
kext). The driver's own messages are otherwise untouched.
UPSTREAM: no.

