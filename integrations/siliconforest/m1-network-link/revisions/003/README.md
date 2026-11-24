# Controlled link003: actual ARM console line endings

Guest equals revision002 (select; executable4f87dec2) exactly. The new host
helper removes trailing CR before LF, including the doubled CR observed in002.
Interior CR stays invalid. The full raw serial capture is unchanged. Frame
CRC32C, nonce, sequence, direction, length and whitespace ingress are unchanged.
Diagnostics count lines normalized and maximum trailing CR. Two host source
changes only re-bind the helper hash; recovery/bootstrap/m1n1 remain unchanged.

The actual Bridge+Ccodec+QEMU host fixture now uses doubled-CR READY and TX
lines and succeeds with real controlled TCP+UDP echoes.16 helper,12 integration
and61 legacy tests pass. No target or publicInternet result is inferred.
