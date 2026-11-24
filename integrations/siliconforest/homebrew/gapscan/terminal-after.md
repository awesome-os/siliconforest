## /Applications/Utilities/Terminal.app/Contents/MacOS/Terminal

Slices: x86_64, arm64e; scanned: arm64e (ptrauth ABI byte 0x80), chained fixups, minos 26.5, sdk 26.5. 37 load commands for libraries (16 weak), 3 of them not in the root.

Fixups dyld must apply: pointer format 12; 11068 rebases (688 authenticated), 2438 binds (1902 authenticated) of 733 imports; 442 imports are bound authenticated at least once (DA address-diversified: 84, IA address-diversified: 360), 306 plain at least once.

**733 imports, 0 missing (not counting weak ones: 0).** Objective-C: 144 classes imported, 0 not defined by any image of the closure; 3271 selectors sent, 213 of them implemented by no image of the closure (a message with one of those cannot be answered; a selector that some class implements may still be missing on the class it is sent to: that is not visible statically).

| Library | In the root | Imports | of them classes | Swift | Missing | Missing (weak) |
|---|---|---|---|---|---|---|
| AppKit.framework | yes | 214 | 99 | 0 | 0 | 0 |
| libSystem.B.dylib | yes | 114 | 0 | 0 | 0 | 0 |
| Foundation.framework | yes | 94 | 49 | 8 | 0 | 0 |
| libswiftCore.dylib | yes | 83 | 2 | 77 | 0 | 0 |
| CoreFoundation.framework | yes | 75 | 18 | 0 | 0 | 0 |
| libobjc.A.dylib | yes | 35 | 2 | 0 | 0 | 0 |
| CoreGraphics.framework | yes | 31 | 0 | 0 | 0 | 0 |
| CoreText.framework | yes | 23 | 0 | 0 | 0 | 0 |
| libicucore.A.dylib | yes | 13 | 0 | 0 | 0 | 0 |
| CoreServices.framework | yes | 12 | 0 | 0 | 0 | 0 |
| ColorSync.framework | yes | 11 | 0 | 0 | 0 | 0 |
| Carbon.framework | yes | 8 | 0 | 0 | 0 | 0 |
| AudioToolbox.framework | yes | 6 | 0 | 0 | 0 | 0 |
| DataDetectorsCore.framework | yes | 4 | 0 | 0 | 0 | 0 |
| QuartzCore.framework | yes | 4 | 2 | 0 | 0 | 0 |
| ApplicationServices.framework | yes | 2 | 0 | 0 | 0 | 0 |
| CoreAnalytics.framework | yes | 2 | 0 | 0 | 0 | 0 |
| CoreAudio.framework | yes | 1 | 0 | 0 | 0 | 0 |
| libncurses.5.4.dylib | yes | 1 | 0 | 0 | 0 | 0 |
| Cocoa.framework | yes | 0 | 0 | 0 | 0 | 0 |
| libScreenReader.dylib | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftCoreAudio.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftCoreFoundation.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftCoreImage.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftCoreMIDI.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftDispatch.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftIOKit.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftMetal.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftOSLog.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftObjectiveC.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftQuartzCore.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftSpatial.dylib (weak) | **no** | 0 | 0 | 0 | 0 | 0 |
| libswiftUniformTypeIdentifiers.dylib (weak) | **no** | 0 | 0 | 0 | 0 | 0 |
| libswiftXPC.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswift_Builtin_float.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |
| libswiftos.dylib (weak) | **no** | 0 | 0 | 0 | 0 | 0 |
| libswiftsimd.dylib (weak) | yes | 0 | 0 | 0 | 0 | 0 |

### Missing symbols by library


### Objective-C classes imported and not defined in the closure (0)

none

### Selectors sent that no image of the closure implements (213 of 3271)

`URLAtIndex:effectiveRange:`, `_isOnDemandSpringLoadingActivated`, `_isOnDemandSpringLoadingArmed`, `_setAppleMenuEnabled:`, `_setOnDemandSpringLoadingEnabled:`, `_setTabRect:`, `_setTabState:`, `_sourceSigningIdentifier`, `_tt_dynamicItemsSeparator:`, `addWindow:`, `animation:didReachProgressMark:`, `animationDidStop:`, `animationShouldStart:`, `application:continueUserActivity:restorationHandler:`, `application:delegateHandlesKey:`, `application:didDecodeRestorableState:`, `application:didFailToContinueUserActivityWithType:error:`, `application:didFailToRegisterForRemoteNotificationsWithError:`, `application:didReceiveRemoteNotification:`, `application:didRegisterForRemoteNotificationsWithDeviceToken:`, `application:didUpdateUserActivity:`, `application:handlerForIntent:`, `application:openFileWithoutUI:`, `application:openFiles:`, `application:openURLs:`, `application:printFile:`, `application:printFiles:withSettings:showPrintPanels:`, `application:userDidAcceptCloudKitShareWithMetadata:`, `application:willContinueUserActivityWithType:`, `application:willEncodeRestorableState:`, `application:willPresentError:`, `applicationDidChangeOcclusionState:`, `applicationDidChangeScreenParameters:`, `applicationDidResignActive:`, `applicationDidUpdate:`, `applicationDockMenu:`, `applicationOpenUntitledFile:`, `applicationProtectedDataDidBecomeAvailable:`, `applicationProtectedDataWillBecomeUnavailable:`, `applicationShouldAutomaticallyLocalizeKeyEquivalents:`, `applicationShouldTerminateAfterLastWindowClosed:`, `applicationWillHide:`, `applicationWillUnhide:`, `applicationWillUpdate:`, `beginScrollGesture`, `commitTabTitleEdit`, `confinementRectForMenu:onScreen:`, `contentAreaDidHide`, `contentAreaDidResize`, `contentAreaWillDraw`, `contextForView:altMode:interactionStartedHandler:interactionChangedHandler:interactionStoppedHandler:`, `contextID`, `control:didFailToFormatString:errorDescription:`, `control:didFailToValidatePartialString:errorDescription:`, `control:isValidObject:`, `control:textShouldBeginEditing:`, `control:textShouldEndEditing:`, `control:textView:completions:forPartialWordRange:indexOfSelectedItem:`, `control:textView:doCommandBySelector:`, `controlTextDidBeginEditing:`, `controlTextDidEndEditing:`, `customWindowsToEnterFullScreenForWindow:`, `customWindowsToEnterFullScreenForWindow:onScreen:`, `customWindowsToExitFullScreenForWindow:`, `didReplaceCharacters`, `didUseActions`, `drawWithRect:options:attributes:`, `drawsVerticallyForCharacterAtIndex:`, `endLiveResize`, `endScrollGesture`, `endTrackingInScrollerImp:`, `fractionOfDistanceThroughGlyphForPoint:`, `gestureRecognizer:shouldAttemptToRecognizeWithEvent:`, `gestureRecognizer:shouldBeRequiredToFailByGestureRecognizer:`, `gestureRecognizer:shouldReceiveTouch:`, `gestureRecognizer:shouldRecognizeSimultaneouslyWithGestureRecognizer:`, `gestureRecognizer:shouldRequireFailureOfGestureRecognizer:`, `gestureRecognizerShouldBegin:`, `handleMouseEvent:`, `immediateActionRecognizerDidUpdateAnimation:`, `insertAdaptiveImageGlyph:replacementRange:`, `isFindBarVisible`, `isLineWrappedToNextLine:`, `lookupAnimationControllerForString:range:options:originProvider:inView:`, `markedTextAbandoned:`, `menu:willHighlightItem:`, `menuDidClose:`, `menuItem:aimPointForPoint:`, `menuItem:maxSizeForPoint:`, `menuItem:preferredEdgeForPoint:`, `menuItem:shouldPositionNearPreviewItemAtScreenPoint:`, `menuItem:viewAtScreenPoint:`, `menuItemWillAppear:`, `menuItemsForResult:actionContext:`, `menuItemsForTargetURL:actionContext:`, `menuWillOpen:`, `mouseEnteredContentArea`, `mouseExitedContentArea`, `mouseMovedInContentArea`, `movedToNewWindow`, `netService:didAcceptConnectionWithInputStream:outputStream:`, `netService:didNotPublish:`, `netService:didUpdateTXTRecordData:`, `netServiceBrowser:didFindDomain:moreComing:`, `netServiceBrowser:didNotSearch:`, `netServiceBrowser:didRemoveDomain:moreComing:`, `netServiceBrowserDidStopSearch:`, `netServiceDidPublish:`, `netServiceDidStop:`, `netServiceWillPublish:`, `netServiceWillResolve:`, `numberOfItemsInMenu:`, `panel:didChangeToDirectoryURL:`, `panel:didSelectType:`, `panel:displayNameForType:`, `panel:shouldEnableURL:`, `panel:userEnteredFilename:confirmed:`, `panel:validateURL:error:`, `panel:willExpand:`, `panelSelectionDidChange:`, `pathControl:acceptDrop:`, `pathControl:shouldDragItem:withPasteboard:`, `pathControl:validateDrop:`, `pathControl:willDisplayOpenPanel:`, `preferredTextAccessoryPlacement`, `previewItemDisplayState`, `previewRepresentableActivityItemsForWindow:`, `pulseAndFade:`, `pulseWithFade:andDissolve:`, `removedFromSuperview`, `runOperationModalForWindow:delegate:didRunSelector:contextInfo:`, `selectedWindow`, `setAimFrame:`, `setAllResults:`, `setColorList:`, `setContentDrawer:`, `setCustomizationDefaultItemIdentifiers:`, `setDefaultHelpBookSearchEnabled:`, `setDefaultItems:`, `setHighlightFrame:`, `setImmediate:`, `setItemIdentifiers:`, `setPreviewStyle:`, `setRects:`, `setSelectedWindow:`, `setSelectionString:`, `setSupportsPressAndHold:`, `setVerticalScrollerImp:`, `sharedManager`, `shouldReplaceCharactersInRanges:withStrings:`, `shouldUseActionsWithContext:`, `showAllHelpTopicsForSearchString:`, `showDefinitionByHotKey`, `showDefinitionForString:range:options:originProvider:inView:`, `showDefinitionForTerm:atLocation:options:`, `showSearchResultsForQueryString:`, `splitView:additionalEffectiveRectOfDividerAtIndex:`, `splitView:canCollapseSubview:`, `splitView:effectiveRect:forDrawnRect:ofDividerAtIndex:`, `splitView:shouldAdjustSizeOfSubview:`, `splitView:shouldCollapseSubview:forDoubleClickOnDividerAtIndex:`, `splitView:shouldHideDividerAtIndex:`, `splitViewWillResizeSubviews:`, `standardQuickLookMenuItem`, `startLiveResize`, `stringAtIndex:effectiveRange:endsWithSearchBoundary:`, `stringLength`, `supportsAdaptiveImageGlyph`, `tabView:shouldSelectTabViewItem:`, `tabViewDidChangeNumberOfTabViewItems:`, `textField:textView:candidates:forSelectedRange:`, `textField:textView:candidatesForSelectedRange:`, `textField:textView:shouldSelectCandidateAtIndex:`, `tokenRangeForString:range:options:`, `unionRectInVisibleSelectedRange`, `wantsToHandleMouseEvents`, `willAddObject:`, `window:didDecodeRestorableState:`, `window:startCustomAnimationToEnterFullScreenOnScreen:withDuration:`, `window:startCustomAnimationToEnterFullScreenWithDuration:`, `window:startCustomAnimationToExitFullScreenWithDuration:`, `window:willResizeForVersionBrowserWithMaxPreferredSize:maxAllowedSize:`, `window:willUseFullScreenPresentationOptions:`, `windowDidChangeBackingProperties:`, `windowDidChangeOcclusionState:`, `windowDidChangeScreen:`, `windowDidChangeScreenProfile:`, `windowDidDeminiaturize:`, `windowDidEndLiveResize:`, `windowDidEndSheet:`, `windowDidEnterFullScreen:`, `windowDidEnterVersionBrowser:`, `windowDidExitFullScreen:`, `windowDidExitVersionBrowser:`, `windowDidExpose:`, `windowDidFailToEnterFullScreen:`, `windowDidFailToExitFullScreen:`, `windowDidMove:`, `windowDidUpdate:`, `windowForSharingRequestFromWindow:`, `windowLevel`, `windowOrderedIn`, `windowOrderedOut`, `windowShouldZoom:toFrame:`, `windowWillBeginSheet:`, `windowWillEnterFullScreen:`, `windowWillEnterVersionBrowser:`, `windowWillExitFullScreen:`, `windowWillExitVersionBrowser:`, `windowWillMove:`, `windowWillResize:toSize:`, `windowWillReturnFieldEditor:toObject:`, `windowWillStartLiveResize:`

