# Gap scan of 138 unpacked bottles against the M1 disk root, before

Made by `../sf_gapscan.py --root <copy of the M1 trees> --kegs runs/homebrew/kegs/arm64_tahoe --brew`: the bottles `../hb_fetch.py` fetched for the 48-formula survey with their dependencies, not installed on the M1. Static only: none of these programs was run for this table.

## Homebrew prefix (/opt/homebrew/Cellar)

1458 Mach-O images checked directly, 562 programs with their load closure. **15 owners (formulae) have at least one program that does not start; 71 programs in all.**

| # | Missing | Kind | Symbols | Formulae with a stopped program | Formulae with the gap in any image | Binding |
|---|---|---|---|---|---|---|
| 1 | AppKit.framework | library absent | `_NSApp`, `_NSAppKitVersionNumber`, `_NSApplicationDidBecomeActiveNotification`, `_NSApplicationDidChangeScreenParametersNotification`, `_NSApplicationMain`, `_NSBackingPropertyOldScaleFactorKey`, `_NSCalibratedRGBColorSpace`, `_NSDeviceRGBColorSpace` … (68) | 3: ffmpeg, starship, vim | 7: ffmpeg, python@3.14, python@3.15, sdl2-compat, sdl3, starship, vim | at load |
| 2 | CoreServices.framework | symbols absent | `_AEBuildAppleEvent`, `_AEDisposeDesc`, `_AEGetDescData`, `_AEGetParamDesc`, `_AESendMessage`, `_CreateTextEncoding`, `_LocaleRefGetPartString`, `_LocaleStringToLangAndRegionCodes` … (12) | 3: cmake, neovim, vim | 5: cmake, neovim, python@3.14, python@3.15, vim | at load |
| 3 | libSystem.B.dylib | symbols absent | `_OSAtomicFifoDequeue`, `_OSAtomicFifoEnqueue`, `_gets`, `_innetgr`, `_login`, `_logout`, `_logwtmp`, `_timingsafe_bcmp` | 2: libfido2, openssh | 4: gcc, libfido2, llvm, openssh | at load |
| 4 | libncurses.5.4.dylib | symbols absent | `_assume_default_colors$NCURSES60`, `_color_content$NCURSES60`, `_copywin$NCURSES60`, `_delay_output$NCURSES60`, `_derwin$NCURSES60`, `_doupdate$NCURSES60`, `_endwin$NCURSES60`, `_flushinp$NCURSES60` … (73) | 2: cmake, pinentry | 4: cmake, pinentry, python@3.14, python@3.15 | at load |
| 5 | Cocoa.framework | library absent | (none imported) | 2: ffmpeg, webp | 3: ffmpeg, sdl3, webp | at load |
| 6 | Kerberos.framework | library absent | `_GSS_C_NT_HOSTBASED_SERVICE`, `_gss_accept_sec_context`, `_gss_acquire_cred`, `_gss_add_oid_set_member`, `_gss_create_empty_oid_set`, `_gss_delete_sec_context`, `_gss_display_name`, `_gss_display_status` … (39) | 2: curl, openssh | 2: curl, openssh | at load |
| 7 | LDAP.framework | library absent | `_ber_bvfree`, `_ber_free`, `_ber_memvfree`, `_ldap_add_s`, `_ldap_control_free`, `_ldap_controls_free`, `_ldap_count_entries`, `_ldap_create_page_control` … (38) | 2: curl, gnupg | 2: curl, gnupg | at load |
| 8 | OpenGL.framework | library absent | `_CGLGetCurrentContext`, `_glBlendFunc`, `_glClear`, `_glClearColor`, `_glColor3ub`, `_glColor4f`, `_glDisable`, `_glDrawPixels` … (22) | 2: ffmpeg, webp | 2: ffmpeg, webp | at load |
| 9 | AVFoundation.framework | library absent | `_AVCaptureDeviceTypeBuiltInWideAngleCamera`, `_AVCaptureDeviceTypeContinuityCamera`, `_AVCaptureDeviceTypeDeskViewCamera`, `_AVCaptureDeviceTypeExternal`, `_AVCaptureDeviceTypeExternalUnknown`, `_AVCaptureDeviceTypeMicrophone`, `_AVCaptureSessionPresetHigh`, `_AVMediaTypeAudio` … (17) | 1: ffmpeg | 2: ffmpeg, sdl3 | at load |
| 10 | CoreGraphics.framework | library absent | `_CGAcquireDisplayFadeReservation`, `_CGAssociateMouseAndMouseCursorPosition`, `_CGBitmapContextCreate`, `_CGColorGetConstantColor`, `_CGColorSpaceCreateWithName`, `_CGColorSpaceRelease`, `_CGContextDrawImage`, `_CGContextRelease` … (45) | 1: ffmpeg | 2: ffmpeg, sdl3 | at load |
| 11 | CoreMedia.framework | library absent | `_CMAudioFormatDescriptionGetStreamBasicDescription`, `_CMBlockBufferCopyDataBytes`, `_CMBlockBufferCreateWithMemoryBlock`, `_CMBlockBufferGetDataLength`, `_CMFormatDescriptionGetExtension`, `_CMFormatDescriptionGetMediaSubType`, `_CMFormatDescriptionGetMediaType`, `_CMSampleBufferCreate` … (27) | 1: ffmpeg | 2: ffmpeg, sdl3 | at load |
| 12 | CoreVideo.framework | library absent | `_CVBufferCopyAttachments`, `_CVBufferPropagateAttachments`, `_CVBufferRemoveAttachment`, `_CVBufferSetAttachment`, `_CVColorPrimariesGetStringForIntegerCodePoint`, `_CVDisplayLinkCreateWithActiveCGDisplays`, `_CVDisplayLinkCreateWithCGDisplay`, `_CVDisplayLinkGetNominalOutputVideoRefreshPeriod` … (79) | 1: ffmpeg | 2: ffmpeg, sdl3 | at load |
| 13 | Metal.framework | library absent | `_MTLCopyAllDevices`, `_MTLCreateSystemDefaultDevice`, `_OBJC_CLASS_$_MTLComputePipelineDescriptor`, `_OBJC_CLASS_$_MTLDepthStencilDescriptor`, `_OBJC_CLASS_$_MTLRenderPassDescriptor`, `_OBJC_CLASS_$_MTLRenderPipelineDescriptor`, `_OBJC_CLASS_$_MTLSamplerDescriptor`, `_OBJC_CLASS_$_MTLStencilDescriptor` … (10) | 1: ffmpeg | 2: ffmpeg, sdl3 | at load |
| 14 | ApplicationServices.framework | library absent | `_TransformProcessType` | 1: ffmpeg | 1: sdl2-compat | at load |
| 15 | CoreFoundation.framework | symbols absent | `___NSDictionary0__struct` | 1: ffmpeg | 1: ffmpeg | at load |
| 16 | CoreImage.framework | library absent | `_OBJC_CLASS_$_CIColor`, `_OBJC_CLASS_$_CIContext`, `_OBJC_CLASS_$_CIFilter`, `_OBJC_CLASS_$_CIImage`, `_OBJC_CLASS_$_CIVector`, `_kCIAttributeClass`, `_kCIAttributeDefault`, `_kCIAttributeSliderMax` … (14) | 1: ffmpeg | 1: ffmpeg | at load |
| 17 | GLUT.framework | library absent | `_glutBitmap9By15`, `_glutBitmapCharacter`, `_glutCreateWindow`, `_glutDisplayFunc`, `_glutGet`, `_glutIdleFunc`, `_glutInit`, `_glutInitDisplayMode` … (14) | 1: webp | 1: webp | at load |
| 18 | Security.framework | symbols absent | `_SecKeychainCopyDefault`, `_SecKeychainItemFreeContent` | 1: rust | 1: rust | at load |
| 19 | VideoToolbox.framework | library absent | `_VTCompressionSessionCompleteFrames`, `_VTCompressionSessionCreate`, `_VTCompressionSessionEncodeFrame`, `_VTCompressionSessionGetPixelBufferPool`, `_VTCompressionSessionPrepareToEncodeFrames`, `_VTCopySupportedPropertyDictionaryForEncoder`, `_VTDecompressionSessionCreate`, `_VTDecompressionSessionDecodeFrame` … (59) | 1: ffmpeg | 1: ffmpeg | at load |
| 20 | libhvf.dylib | library absent | `_HVF_clear_part_cache`, `_HVF_close_part_renderer`, `_HVF_open_part_renderer`, `_HVF_part_renderer_storage_size`, `_HVF_render_current_part`, `_HVF_render_part_axis_count`, `_HVF_set_axis_value`, `_HVF_set_render_part` | 1: imagemagick | 1: freetype | at load |
| 21 | libpam.2.dylib | library absent | `_pam_acct_mgmt`, `_pam_authenticate`, `_pam_chauthtok`, `_pam_close_session`, `_pam_end`, `_pam_get_item`, `_pam_getenvlist`, `_pam_open_session` … (13) | 1: openssh | 1: openssh | at load |
| 22 | libpcap.A.dylib | library absent | `_pcap_close`, `_pcap_dump`, `_pcap_dump_close`, `_pcap_dump_open`, `_pcap_next`, `_pcap_open_offline` | 1: ldns | 1: ldns | at load |
| 23 | libperl.dylib | library absent | `_PL_thr_key`, `_PerlIOBase_pushed`, `_PerlIO_define_layer`, `_Perl_av_fetch`, `_Perl_av_len`, `_Perl_call_argv`, `_Perl_call_method`, `_Perl_call_pv` … (57) | 1: vim | 1: vim | at load |
| 24 | libsandbox.1.dylib | library absent | (none imported) | 1: openssh | 1: openssh | at load |
| 25 | libxcselect.dylib | library absent | `_xcselect_host_sdk_path` | 1: llvm | 1: llvm | at load |
| 26 | Carbon.framework | library absent | `_GetCurrentKeyModifiers`, `_KBGetLayoutType`, `_LMGetKbdType`, `_TISCopyCurrentKeyboardLayoutInputSource`, `_TISGetInputSourceProperty`, `_kTISPropertyUnicodeKeyLayoutData` | 0:  | 3: python@3.14, python@3.15, sdl3 | - |
| 27 | libexpat.1.dylib | symbols absent | `_XML_ErrorString`, `_XML_ExpatVersion`, `_XML_ExpatVersionInfo`, `_XML_ExternalEntityParserCreate`, `_XML_FreeContentModel`, `_XML_GetBase`, `_XML_GetBuffer`, `_XML_GetCurrentByteIndex` … (57) | 0:  | 3: git, python@3.14, python@3.15 | - |
| 28 | ForceFeedback.framework | library absent | `_FFCreateDevice`, `_FFDeviceCreateEffect`, `_FFDeviceGetForceFeedbackCapabilities`, `_FFDeviceGetForceFeedbackProperty`, `_FFDeviceReleaseEffect`, `_FFDeviceSendForceFeedbackCommand`, `_FFDeviceSetForceFeedbackProperty`, `_FFEffectGetEffectStatus` … (13) | 0:  | 1: sdl3 | - |
| 29 | GameController.framework | library absent | `_GCControllerDidConnectNotification`, `_GCControllerDidDisconnectNotification`, `_GCHapticDurationInfinite`, `_GCHapticsLocalityHandles`, `_GCHapticsLocalityLeftHandle`, `_GCHapticsLocalityLeftTrigger`, `_GCHapticsLocalityRightHandle`, `_GCHapticsLocalityRightTrigger` … (35) | 0:  | 1: sdl3 | - |
| 30 | OpenAL.framework | library absent | `_alBufferData`, `_alDeleteBuffers`, `_alDeleteSources`, `_alGenBuffers`, `_alGenSources`, `_alGetSourcei`, `_alIsExtensionPresent`, `_alSourcePlay` … (16) | 0:  | 1: mpg123 | - |
| 31 | QuartzCore.framework | library absent | `_OBJC_CLASS_$_CAMetalLayer` | 0:  | 1: sdl3 | - |
| 32 | libobjc.A.dylib | symbols absent | `_objc_claimAutoreleasedReturnValue` | 0:  | 1: sdl3 | - |

### Programs that do not start, by formula

| Formula | Programs | First gaps |
|---|---|---|
| cmake | 2: ccmake, cpack | symbol libncurses.5.4.dylib:_copywin$NCURSES60; symbol libncurses.5.4.dylib:_derwin$NCURSES60; symbol libncurses.5.4.dylib:_endwin$NCURSES60; symbol libncurses.5.4.dylib:_init_pair$NCURSES60 … (24) |
| curl | 1: curl | library Kerberos.framework; library LDAP.framework |
| ffmpeg | 20: aviocat, crypto_bench, enc_recon_frame_test, enum_options, ffescape, ffeval … | library CoreMedia.framework; library CoreVideo.framework; library VideoToolbox.framework |
| gnupg | 1: dirmngr | library LDAP.framework |
| imagemagick | 1: magick | library libhvf.dylib |
| ldns | 1: ldns-dpa | library libpcap.A.dylib |
| libfido2 | 3: fido2-assert, fido2-cred, fido2-token | symbol libSystem.B.dylib:_timingsafe_bcmp |
| llvm | 27: clang-23, clang-apply-replacements, clang-change-namespace, clang-check, clang-doc, clang-extdef-mapping … | library libxcselect.dylib |
| neovim | 1: nvim | symbol CoreServices.framework:_LocaleRefGetPartString |
| openssh | 8: scp, sftp, ssh, ssh-add, ssh-agent, ssh-keygen … | symbol libSystem.B.dylib:_timingsafe_bcmp |
| pinentry | 1: pinentry-curses | symbol libncurses.5.4.dylib:_endwin$NCURSES60; symbol libncurses.5.4.dylib:_init_pair$NCURSES60; symbol libncurses.5.4.dylib:_initscr$NCURSES60; symbol libncurses.5.4.dylib:_keypad$NCURSES60 … (19) |
| rust | 1: cargo | symbol Security.framework:_SecKeychainCopyDefault; symbol Security.framework:_SecKeychainItemFreeContent |
| starship | 1: starship | library AppKit.framework |
| vim | 2: vim, vim | library AppKit.framework; library libperl.dylib; symbol CoreServices.framework:_CreateTextEncoding; symbol CoreServices.framework:_LocaleRefGetPartString … (8) |
| webp | 1: vwebp | library Cocoa.framework; library GLUT.framework; library OpenGL.framework |

### Interpreters that scripts in the prefix name (outside the prefix)

| Interpreter | In the root | Formulae | Scripts |
|---|---|---|---|
| `/usr/bin/env perl` | **no** | 10: curl, ffmpeg, git, go, jemalloc, llvm, ncurses, openssl@3, openssl@4, vim | 41 |
| `/usr/bin/env python` | **no** | 6: git, libevent, libpsl, llvm, python@3.14, python@3.15 | 25 |
| `/usr/bin/env ruby` | **no** | 3: ffmpeg, portable-ruby, ruby | 40 |
| `/usr/bin/perl` | **no** | 3: git, go, vim | 35 |
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
| `/usr/bin/env bash` | yes | 9: cmake, fzf, go, node, portable-ruby, python@3.14, python@3.15, ruby, rust | 50 |
| `/bin/bash` | yes | 8: ca-certificates, ffmpeg, go, node, python@3.14, python@3.15, rust, z3 | 44 |
| `/usr/bin/env python3` | yes | 7: ffmpeg, llvm, nmap, node, python@3.14, python@3.15, rust | 126 |
| `/usr/bin/env sh` | yes | 3: libtool, node, rust | 17 |
| `/bin/zsh` | yes | 1: zsh | 8 |
| `/usr/bin/env lua` | yes | 1: lpeg | 2 |
| `/usr/bin/env node` | yes | 1: node | 24 |
