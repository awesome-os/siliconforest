# Gap scan of 138 unpacked bottles against the M1 disk root, with the fixes of this job

As `wide-before.md`, with the staged fixes laid over the root.

## Homebrew prefix (/opt/homebrew/Cellar)

1458 Mach-O images checked directly, 562 programs with their load closure. **10 owners (formulae) have at least one program that does not start; 56 programs in all.**

| # | Missing | Kind | Symbols | Formulae with a stopped program | Formulae with the gap in any image | Binding |
|---|---|---|---|---|---|---|
| 1 | Kerberos.framework | library absent | `_GSS_C_NT_HOSTBASED_SERVICE`, `_gss_accept_sec_context`, `_gss_acquire_cred`, `_gss_add_oid_set_member`, `_gss_create_empty_oid_set`, `_gss_delete_sec_context`, `_gss_display_name`, `_gss_display_status` … (39) | 2: curl, openssh | 2: curl, openssh | at load |
| 2 | LDAP.framework | library absent | `_ber_bvfree`, `_ber_free`, `_ber_memvfree`, `_ldap_add_s`, `_ldap_control_free`, `_ldap_controls_free`, `_ldap_count_entries`, `_ldap_create_page_control` … (38) | 2: curl, gnupg | 2: curl, gnupg | at load |
| 3 | CoreServices.framework | symbols absent | `_AEBuildAppleEvent`, `_AEDisposeDesc`, `_AEGetDescData`, `_AEGetParamDesc`, `_AESendMessage`, `_LocaleStringToLangAndRegionCodes` | 1: cmake | 3: cmake, python@3.14, python@3.15 | at load |
| 4 | AVFoundation.framework | library absent | `_AVCaptureDeviceTypeBuiltInWideAngleCamera`, `_AVCaptureDeviceTypeContinuityCamera`, `_AVCaptureDeviceTypeDeskViewCamera`, `_AVCaptureDeviceTypeExternal`, `_AVCaptureDeviceTypeExternalUnknown`, `_AVCaptureDeviceTypeMicrophone`, `_AVCaptureSessionPresetHigh`, `_AVMediaTypeAudio` … (17) | 1: ffmpeg | 2: ffmpeg, sdl3 | at load |
| 5 | CoreMedia.framework | library absent | `_CMAudioFormatDescriptionGetStreamBasicDescription`, `_CMBlockBufferCopyDataBytes`, `_CMBlockBufferCreateWithMemoryBlock`, `_CMBlockBufferGetDataLength`, `_CMFormatDescriptionGetExtension`, `_CMFormatDescriptionGetMediaSubType`, `_CMFormatDescriptionGetMediaType`, `_CMSampleBufferCreate` … (27) | 1: ffmpeg | 2: ffmpeg, sdl3 | at load |
| 6 | Metal.framework | library absent | `_MTLCopyAllDevices`, `_MTLCreateSystemDefaultDevice`, `_OBJC_CLASS_$_MTLComputePipelineDescriptor`, `_OBJC_CLASS_$_MTLDepthStencilDescriptor`, `_OBJC_CLASS_$_MTLRenderPassDescriptor`, `_OBJC_CLASS_$_MTLRenderPipelineDescriptor`, `_OBJC_CLASS_$_MTLSamplerDescriptor`, `_OBJC_CLASS_$_MTLStencilDescriptor` … (10) | 1: ffmpeg | 2: ffmpeg, sdl3 | at load |
| 7 | ApplicationServices.framework | library absent | `_TransformProcessType` | 1: ffmpeg | 1: sdl2-compat | at load |
| 8 | CoreFoundation.framework | symbols absent | `___NSDictionary0__struct` | 1: ffmpeg | 1: ffmpeg | at load |
| 9 | CoreImage.framework | library absent | `_OBJC_CLASS_$_CIColor`, `_OBJC_CLASS_$_CIContext`, `_OBJC_CLASS_$_CIFilter`, `_OBJC_CLASS_$_CIImage`, `_OBJC_CLASS_$_CIVector`, `_kCIAttributeClass`, `_kCIAttributeDefault`, `_kCIAttributeSliderMax` … (14) | 1: ffmpeg | 1: ffmpeg | at load |
| 10 | CoreVideo.framework | symbols absent | `_CVColorPrimariesGetStringForIntegerCodePoint`, `_CVImageBufferCreateColorSpaceFromAttachments`, `_CVMetalTextureCacheCreate`, `_CVMetalTextureCacheCreateTextureFromImage`, `_CVMetalTextureGetTexture`, `_CVPixelBufferPoolCreate`, `_CVPixelBufferPoolCreatePixelBuffer`, `_CVPixelBufferPoolRelease` … (24) | 1: ffmpeg | 1: ffmpeg | at load |
| 11 | GLUT.framework | library absent | `_glutBitmap9By15`, `_glutBitmapCharacter`, `_glutCreateWindow`, `_glutDisplayFunc`, `_glutGet`, `_glutIdleFunc`, `_glutInit`, `_glutInitDisplayMode` … (14) | 1: webp | 1: webp | at load |
| 12 | OpenGL.framework | library absent | `_glBlendFunc`, `_glClear`, `_glClearColor`, `_glColor3ub`, `_glColor4f`, `_glDisable`, `_glDrawPixels`, `_glEnable` … (21) | 1: webp | 1: webp | at load |
| 13 | Security.framework | symbols absent | `_SecKeychainCopyDefault`, `_SecKeychainItemFreeContent` | 1: rust | 1: rust | at load |
| 14 | VideoToolbox.framework | library absent | `_VTCompressionSessionCompleteFrames`, `_VTCompressionSessionCreate`, `_VTCompressionSessionEncodeFrame`, `_VTCompressionSessionGetPixelBufferPool`, `_VTCompressionSessionPrepareToEncodeFrames`, `_VTCopySupportedPropertyDictionaryForEncoder`, `_VTDecompressionSessionCreate`, `_VTDecompressionSessionDecodeFrame` … (59) | 1: ffmpeg | 1: ffmpeg | at load |
| 15 | libhvf.dylib | library absent | `_HVF_clear_part_cache`, `_HVF_close_part_renderer`, `_HVF_open_part_renderer`, `_HVF_part_renderer_storage_size`, `_HVF_render_current_part`, `_HVF_render_part_axis_count`, `_HVF_set_axis_value`, `_HVF_set_render_part` | 1: imagemagick | 1: freetype | at load |
| 16 | libpam.2.dylib | library absent | `_pam_acct_mgmt`, `_pam_authenticate`, `_pam_chauthtok`, `_pam_close_session`, `_pam_end`, `_pam_get_item`, `_pam_getenvlist`, `_pam_open_session` … (13) | 1: openssh | 1: openssh | at load |
| 17 | libpcap.A.dylib | library absent | `_pcap_close`, `_pcap_dump`, `_pcap_dump_close`, `_pcap_dump_open`, `_pcap_next`, `_pcap_open_offline` | 1: ldns | 1: ldns | at load |
| 18 | libsandbox.1.dylib | library absent | (none imported) | 1: openssh | 1: openssh | at load |
| 19 | libxcselect.dylib | library absent | `_xcselect_host_sdk_path` | 1: llvm | 1: llvm | at load |
| 20 | libSystem.B.dylib | symbols absent | `_OSAtomicFifoDequeue`, `_OSAtomicFifoEnqueue`, `_gets`, `_innetgr`, `_login`, `_logout`, `_logwtmp` | 0:  | 3: gcc, llvm, openssh | - |
| 21 | AppKit.framework | symbols absent | `_NSPasteboardURLReadingFileURLsOnlyKey` | 0:  | 1: sdl3 | - |
| 22 | CoreGraphics.framework | symbols absent | `_CGAcquireDisplayFadeReservation`, `_CGDisplayFade`, `_CGDisplayMoveCursorToPoint`, `_CGReleaseDisplayFadeReservation` | 0:  | 1: sdl3 | - |
| 23 | ForceFeedback.framework | library absent | `_FFCreateDevice`, `_FFDeviceCreateEffect`, `_FFDeviceGetForceFeedbackCapabilities`, `_FFDeviceGetForceFeedbackProperty`, `_FFDeviceReleaseEffect`, `_FFDeviceSendForceFeedbackCommand`, `_FFDeviceSetForceFeedbackProperty`, `_FFEffectGetEffectStatus` … (13) | 0:  | 1: sdl3 | - |
| 24 | GameController.framework | library absent | `_GCControllerDidConnectNotification`, `_GCControllerDidDisconnectNotification`, `_GCHapticDurationInfinite`, `_GCHapticsLocalityHandles`, `_GCHapticsLocalityLeftHandle`, `_GCHapticsLocalityLeftTrigger`, `_GCHapticsLocalityRightHandle`, `_GCHapticsLocalityRightTrigger` … (35) | 0:  | 1: sdl3 | - |
| 25 | OpenAL.framework | library absent | `_alBufferData`, `_alDeleteBuffers`, `_alDeleteSources`, `_alGenBuffers`, `_alGenSources`, `_alGetSourcei`, `_alIsExtensionPresent`, `_alSourcePlay` … (16) | 0:  | 1: mpg123 | - |
| 26 | libobjc.A.dylib | symbols absent | `_objc_claimAutoreleasedReturnValue` | 0:  | 1: sdl3 | - |

### Programs that do not start, by formula

| Formula | Programs | First gaps |
|---|---|---|
| cmake | 1: cpack | symbol CoreServices.framework:_LocaleStringToLangAndRegionCodes |
| curl | 1: curl | library Kerberos.framework; library LDAP.framework |
| ffmpeg | 20: aviocat, crypto_bench, enc_recon_frame_test, enum_options, ffescape, ffeval … | library CoreMedia.framework; library VideoToolbox.framework; symbol CoreVideo.framework:_CVColorPrimariesGetStringForIntegerCodePoint; symbol CoreVideo.framework:_CVImageBufferCreateColorSpaceFromAttachments … (18) |
| gnupg | 1: dirmngr | library LDAP.framework |
| imagemagick | 1: magick | library libhvf.dylib |
| ldns | 1: ldns-dpa | library libpcap.A.dylib |
| llvm | 27: clang-23, clang-apply-replacements, clang-change-namespace, clang-check, clang-doc, clang-extdef-mapping … | library libxcselect.dylib |
| openssh | 2: ssh, sshd | library Kerberos.framework |
| rust | 1: cargo | symbol Security.framework:_SecKeychainCopyDefault; symbol Security.framework:_SecKeychainItemFreeContent |
| webp | 1: vwebp | library GLUT.framework; library OpenGL.framework |

### Interpreters that scripts in the prefix name (outside the prefix)

| Interpreter | In the root | Formulae | Scripts |
|---|---|---|---|
| `/usr/bin/env python` | **no** | 6: git, libevent, libpsl, llvm, python@3.14, python@3.15 | 25 |
| `/usr/bin/env ruby` | **no** | 3: ffmpeg, portable-ruby, ruby | 40 |
| `/usr/bin/python` | **no** | 3: python@3.14, python@3.15, vim | 6 |
| `./python` | **no** | 2: python@3.14, python@3.15 | 3 |
| `/usr/bin/env Bundler.settings[:shebang]` | **no** | 2: portable-ruby, ruby | 4 |
| `/usr/bin/env python3.8` | **no** | 2: python@3.14, python@3.15 | 6 |
| `./perl` | **no** | 1: perl | 1 |
| `/bin/csh` | **no** | 1: vim | 2 |
| `/bin/rc` | **no** | 1: go | 4 |
| `/usr/bin/awk` | **no** | 1: vim | 2 |
| `/usr/bin/make` | **no** | 1: ncurses | 6 |
| `/usr/bin/python3` | **no** | 1: git | 2 |
| `/usr/bin/ruby` | **no** | 1: ruby | 2 |
| `/usr/local/bin/ruby` | **no** | 1: ruby | 3 |
| `/usr/local/bin/zsh` | **no** | 1: zsh | 2 |
| `[allow(` | **no** | 1: rust | 2 |
| `[allow(clippy::all)]` | **no** | 1: rust | 2 |
| `[allow(clippy::approx_constant)]` | **no** | 1: rust | 2 |
| `[allow(clippy::enum_clike_unportable_variant)]` | **no** | 1: rust | 2 |
| `[allow(clippy::from_over_into)]` | **no** | 1: rust | 2 |
| `[allow(clippy::len_without_is_empty)]` | **no** | 1: rust | 2 |
| `[allow(clippy::needless_late_init)]` | **no** | 1: rust | 2 |
| `[allow(clippy::useless_conversion)]` | **no** | 1: rust | 2 |
| `[allow(dead_code)]` | **no** | 1: rust | 20 |
| `[allow(deprecated)]` | **no** | 1: rust | 8 |
| `[allow(improper_ctypes_definitions)]` | **no** | 1: rust | 1 |
| `[allow(internal_features,` | **no** | 1: rust | 4 |
| `[allow(invalid_from_utf8)]` | **no** | 1: rust | 2 |
| `[allow(missing_debug_implementations)]` | **no** | 1: rust | 2 |
| `[allow(missing_docs)]` | **no** | 1: rust | 4 |
| `[allow(missing_docs,` | **no** | 1: rust | 4 |
| `[allow(non_camel_case_types)]` | **no** | 1: rust | 2 |
| `[allow(non_upper_case_globals,` | **no** | 1: rust | 2 |
| `[allow(nonstandard_style)]` | **no** | 1: rust | 8 |
| `[allow(overflowing_literals)]` | **no** | 1: rust | 2 |
| `[allow(undropped_manually_drops)]` | **no** | 1: rust | 2 |
| `[allow(unexpected_cfgs)]` | **no** | 1: rust | 2 |
| `[allow(unreachable_code)]` | **no** | 1: rust | 2 |
| `[allow(unsafe_op_in_unsafe_fn)]` | **no** | 1: rust | 4 |
| `[allow(unused)]` | **no** | 1: rust | 10 |
| `[allow(unused,` | **no** | 1: rust | 2 |
| `[allow(unused_features)]` | **no** | 1: rust | 4 |
| `[allow(unused_imports)]` | **no** | 1: rust | 2 |
| `[allow(unused_imports,` | **no** | 1: rust | 2 |
| `[allow(unused_macros)]` | **no** | 1: rust | 4 |
| `[allow(unused_macros,` | **no** | 1: rust | 4 |
| `[allow(unused_must_use)]` | **no** | 1: rust | 2 |
| `[allow(unused_unsafe)]` | **no** | 1: rust | 2 |
| `[allow(unused_variables)]` | **no** | 1: rust | 2 |
| `[allow(warnings)]` | **no** | 1: rust | 2 |
| `[cfg(all(` | **no** | 1: rust | 6 |
| `[cfg(all(feature` | **no** | 1: rust | 12 |
| `[cfg(all(target_env` | **no** | 1: rust | 2 |
| `[cfg(all(test,` | **no** | 1: rust | 2 |
| `[cfg(any(target_arch` | **no** | 1: rust | 2 |
| `[cfg(any(target_family` | **no** | 1: rust | 2 |
| `[cfg(dbginfo` | **no** | 1: rust | 2 |
| `[cfg(feature` | **no** | 1: rust | 6 |
| `[cfg(not(all(windows,` | **no** | 1: rust | 2 |
| `[cfg(not(any(` | **no** | 1: rust | 2 |
| `[cfg(not(target_arch` | **no** | 1: rust | 2 |
| `[cfg(not(target_env` | **no** | 1: rust | 2 |
| `[cfg(not(test))]` | **no** | 1: rust | 2 |
| `[cfg(test)]` | **no** | 1: rust | 4 |
| `[cfg(windows)]` | **no** | 1: rust | 6 |
| `[cfg_attr(` | **no** | 1: rust | 2 |
| `[cfg_attr(any(feature` | **no** | 1: rust | 2 |
| `[cfg_attr(f128_enabled,` | **no** | 1: rust | 18 |
| `[cfg_attr(f16_enabled,` | **no** | 1: rust | 4 |
| `[cfg_attr(feature` | **no** | 1: rust | 2 |
| `[cfg_attr(target_family` | **no** | 1: rust | 4 |
| `[cfg_attr(test,` | **no** | 1: rust | 12 |
| `[deny(missing_docs)]` | **no** | 1: rust | 2 |
| `[deny(rust_2018_idioms)]` | **no** | 1: rust | 2 |
| `[deny(unsafe_op_in_unsafe_fn)]` | **no** | 1: rust | 8 |
| `[deny(warnings)]` | **no** | 1: rust | 2 |
| `[doc` | **no** | 1: rust | 10 |
| `[doc(hidden)]` | **no** | 1: rust | 2 |
| `[expect(missing_docs)]` | **no** | 1: rust | 10 |
| `[expect(non_camel_case_types)]` | **no** | 1: rust | 2 |
| `[feature(alloc_error_hook,` | **no** | 1: rust | 2 |
| `[feature(cfg_target_thread_local)]` | **no** | 1: rust | 2 |
| `[feature(const_default)]` | **no** | 1: rust | 2 |
| `[feature(duration_constants)]` | **no** | 1: rust | 2 |
| `[feature(error_generic_member_access,` | **no** | 1: rust | 2 |
| `[feature(f128)]` | **no** | 1: rust | 2 |
| `[feature(f16)]` | **no** | 1: rust | 2 |
| `[feature(no_core)]` | **no** | 1: rust | 4 |
| `[feature(pattern)]` | **no** | 1: rust | 2 |
| `[feature(portable_simd)]` | **no** | 1: rust | 54 |
| `[feature(restricted_std)]` | **no** | 1: rust | 2 |
| `[feature(test)]` | **no** | 1: rust | 6 |
| `[feature(thread_sleep_until)]` | **no** | 1: rust | 2 |
| `[forbid(unsafe_op_in_unsafe_fn)]` | **no** | 1: rust | 30 |
| `[no_std]` | **no** | 1: rust | 8 |
| `[stable(feature` | **no** | 1: rust | 76 |
| `[unstable(` | **no** | 1: rust | 4 |
| `[unstable(feature` | **no** | 1: rust | 28 |
| `[unstable(issue` | **no** | 1: rust | 6 |
| `[unstable(reason` | **no** | 1: rust | 6 |
| `perl` | **no** | 1: perl | 4 |
| `/bin/sh` | yes | 39: bash, cmake, curl, ffmpeg, findutils, freetype, gawk, gcc, gettext, git … | 422 |
| `/usr/bin/env perl` | yes | 10: curl, ffmpeg, git, go, jemalloc, llvm, ncurses, openssl@3, openssl@4, vim | 41 |
| `/usr/bin/env bash` | yes | 9: cmake, fzf, go, node, portable-ruby, python@3.14, python@3.15, ruby, rust | 50 |
| `/bin/bash` | yes | 8: ca-certificates, ffmpeg, go, node, python@3.14, python@3.15, rust, z3 | 44 |
| `/usr/bin/env python3` | yes | 7: ffmpeg, llvm, nmap, node, python@3.14, python@3.15, rust | 126 |
| `/usr/bin/env sh` | yes | 3: libtool, node, rust | 17 |
| `/usr/bin/perl` | yes | 3: git, go, vim | 35 |
| `/bin/zsh` | yes | 1: zsh | 8 |
| `/usr/bin/env lua` | yes | 1: lpeg | 2 |
| `/usr/bin/env node` | yes | 1: node | 24 |
