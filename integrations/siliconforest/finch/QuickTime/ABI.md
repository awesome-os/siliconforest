# Private-provider call boundaries

Target: cached QuickTime Player26.5.2 arm64e slice, executable SHA256
9fff9ad29b20ab75f88a4e3f4d7515e50852ffc8e199e08c2ff3d3c6e8d5c203.
REA opened this artifact; no deep provider is installed/configured. The table
below records ordinary local LLVM call-site observations, not decompilation or
execution. Raw existing disassembly remains ignored in runs/sffinch.

| Entry | Observed QuickTime boundary | Local site | Provider refusal |
|---|---|---|---|
| TCCAccessPreflight | x0 service string, x1 options(NULL); w0 compared with0/1/2 | 0x100039418 | 1, denied |
| TCCAccessRequest | x0 service, x1 options(NULL), x2 Objective-C block; result unused | 0x10003973c | asynchronous false once; no prompt |
| MRMediaRemoteGetLocalOrigin | no new arguments established; pointer result passed onward | 0x100011ba8 | NULL, no origin |
| MRMediaRemoteSetNowPlayingTintColor | x0 origin, s0..s3 four floats; result unused | 0x100011bc8 | explicit unavailable diagnostic |
| GEOManagedObjectContext | w0 Boolean(1), x0 object/pointer result retained | 0x10007c368 | nil, no store |
| PAMarkOperationStart | x0/x1 pointer-sized arguments; result unused | 0x10001c170 | no recording token |
| PAMarkOperationEnd | pointer-sized arguments forwarded by a tail branch | 0x100035db4 | no recording backend |
| FigAssetCreateWithURL | address only registered as a function value | 0x100003140 | **not exported**, unsafe ABI/ownership unknown |

These observations establish only the current caller's argument lanes and use
of results. They do not prove all signatures, ownership, service enums or wider
private callers. In particular, the PerformanceAnalysis result is not observed
by this app, and MediaRemote's void setter has no error return channel.

The Linux-built metadata-only reference on M5 supplies Objective-C method
encodings (296lines) and no native service operations. ABI-relevant examples:

* PFSlowMotionUtilities' source-duration is double, rate is float, followed by
  our48-byte CMTimeRange and a Boolean. A CMTime duration declaration here would
  be wrong. The refusal returns nil.
* PFVideoAVObjectBuilder's asset/adjustments initializer accepts two objects and
  returns an object. The refusal releases the allocated shell and returns nil.
* PFVideoAdjustments' initializer takes CMTimeRange then float; no private
  adjustment object is fabricated.
* MAAssetQuery's synchronous status is signed64-bit; queryMetaData: accepts a
  block. The queryMetaDataWithError: spelling is also a block, **not NSError**.
  Its callback internals are not established by method metadata and this
  minimal provider does not export a guessed implementation of that method.
* Native MobileAsset's version key is _ContentVersion. Queries/catalog
  downloads fail, empty results do not imply a successful catalog fetch.
* MPDisplayMgr's sharedMgr returns an object. nil represents the unavailable
  private monitor manager, not a fabricated display inventory.

The selected framework compatibility versions match this app's requested1.0.
Swift FeatureFlagsKey protocol metadata and property-requirement descriptors
are emitted by the actual Swift compiler from Finch's public protocol shape;
no hand-built Swift metadata or fake mangled-name aliases are supplied.

CoreMediaIO's additional loader barrier has a public, seven-argument contract:
uint32 object ID, pointer to three uint32 address fields, uint32 qualifier byte
count, const qualifier pointer, uint32 data byte count, uint32 used-byte pointer,
and output pointer; result is signed OSStatus. Our declarations are written
here rather than copied from SDK headers. The absence implementation returns
-4 (unimplemented), writes zero only to a non-NULL used-byte pointer, and leaves
the data buffer untouched. The M1 fixture tests both a filled output buffer and
a NULL size query. It creates no camera objects or successful property result.

The latest actual dyld result is runs/sffinch/quicktime-load-v4/load.json:
Quartz.framework is absent. This precedes symbol binding and startup, so the
successful private-provider contract tests do not imply QuickTime can launch.
In particular FigAssetCreateWithURL's ABI is still unknown and absent, and
SwiftUI/other unowned imports remain outside these eight private providers.
