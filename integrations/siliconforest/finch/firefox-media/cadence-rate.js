// SPDX-License-Identifier: MIT
// Own fixture diagnostic: test a playback-clock prediction, never change global
// browser preferences, display pacing or production media behavior.
(() => {
  const requested = Number(new URLSearchParams(location.search).get('rate') || 1);
  if (![0.5, 1, 2].includes(requested)) throw new Error('Unsupported diagnostic rate');
  const element = document.querySelector('video');
  const original = window.fxMeasureVideo;
  if (!element || typeof original !== 'function') throw new Error('Missing video fixture');
  window.fxMeasureVideo = async () => {
    element.defaultPlaybackRate = requested;
    element.playbackRate = requested;
    window.fxVideoResult.requestedPlaybackRate = requested;
    const result = await original();
    window.fxVideoResult.actualPlaybackRate = element.playbackRate;
    if (result && typeof result === 'object') result.actualPlaybackRate = element.playbackRate;
    return result;
  };
})();
