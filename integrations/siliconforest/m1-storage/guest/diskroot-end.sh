# ---- disk root: the end. The storage proof of guest/darwin-diskroot.in (root, fence, tree, ranges),
# which ends by stopping the controller from one process already in memory and printing the
# bench's end marker: nothing can be read from the root after the stop.
exec /bin/sh /private/ram/usr/bin/dr-diskroot-end "$nonce"
