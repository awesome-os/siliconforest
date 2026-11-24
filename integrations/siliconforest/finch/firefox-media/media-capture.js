// SPDX-License-Identifier: MIT
// Three own-fixture repaints AFTER playback, solely to serve a renderer capture.
(() => {
  let capture = null;
  window.fxArmMediaCapture = () => {
    const result = window.fxVideoResult, video = document.querySelector('video');
    if (capture || !video?.ended || !Number.isFinite(result?.measureEnd) ||
        result.measureEnd >= performance.now()) throw new Error('Capture requires completed own playback');
    // The runner may have scrolled the video into view: headings and output
    // below it can both be outside the viewport. Use a bounded visible badge.
    const node = document.createElement('div');
    node.style.cssText = 'position:fixed;right:8px;top:8px;z-index:2147483647;background:#14332b;color:white;font:14px monospace;padding:4px;pointer-events:none';
    document.body.append(node);
    const state = {start: performance.now(), updates: 0, finishBy: performance.now() + 650};
    capture = {state, node};
    for (const delay of [100, 300, 600]) setTimeout(() => {
      node.textContent = 'Completed video capture ' + (++state.updates);
    }, delay);
    return state;
  };
  window.fxFinishMediaCapture = async () => {
    if (!capture) throw new Error('Capture was not armed');
    const {state, node} = capture;
    await new Promise(resolve => setTimeout(resolve, Math.max(0, state.finishBy - performance.now())));
    node.remove();
    await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    capture = null;
    if (state.updates !== 3) throw new Error('Own capture repaints did not complete');
    state.end = performance.now();
    return state;
  };
})();
