/* SPDX-License-Identifier: MIT */
/* Inject only into our owned local test page. Capability declarations are not
 * decode, HDR correctness or speed evidence; compare with actual playback. */
window.fxCodecCapabilities = async function () {
  const video = document.createElement('video');
  const audio = document.createElement('audio');
  const configs = [
    ['h264-1080p60', 'video/mp4; codecs="avc1.64002A"', 1920, 1080, 60, 8000000],
    ['hevc-4k30-8bit', 'video/mp4; codecs="hvc1.1.6.L150.B0"', 3840, 2160, 30, 16000000],
    ['vp9-1080p60', 'video/webm; codecs="vp09.00.41.08"', 1920, 1080, 60, 6000000],
    ['vp9-4k30', 'video/webm; codecs="vp09.00.50.08"', 3840, 2160, 30, 16000000],
    ['av1-1080p30', 'video/webm; codecs="av01.0.08M.08"', 1920, 1080, 30, 4000000],
    ['av1-1080p60', 'video/webm; codecs="av01.0.09M.08"', 1920, 1080, 60, 6000000],
    ['hevc-main10-hdr', 'video/mp4; codecs="hvc1.2.4.L150.B0"', 3840, 2160, 30, 18000000],
    ['vp9-profile2-hdr', 'video/webm; codecs="vp09.02.50.10"', 3840, 2160, 30, 16000000]
  ].map(([name, contentType, width, height, framerate, bitrate]) => ({name,
    video: {contentType, width, height, framerate, bitrate,
      ...(name.endsWith('-hdr') ? {colorGamut: 'rec2020', transferFunction: 'pq'} : {})}}));
  configs.push(
    {name: 'aac-stereo-48k', audio: {contentType: 'audio/mp4; codecs="mp4a.40.2"', channels: '2', bitrate: 128000, samplerate: 48000}},
    {name: 'opus-stereo-48k', audio: {contentType: 'audio/webm; codecs="opus"', channels: '2', bitrate: 128000, samplerate: 48000}}
  );
  const deadline = async promise => {
    let timer;
    try { return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error('Capability query exceeded 3 s')), 3000);
    })]); } finally { clearTimeout(timer); }
  };
  const results = [];
  for (const {name, ...config} of configs) {
    const type = config.video?.contentType ?? config.audio.contentType;
    const row = {name, configuration: config, canPlayType: (config.video ? video : audio).canPlayType(type),
      mseTypeSupported: typeof MediaSource === 'undefined' ? null : MediaSource.isTypeSupported(type), queries: []};
    for (const mode of ['file', 'media-source']) {
      const start = performance.now();
      try {
        if (!navigator.mediaCapabilities?.decodingInfo) throw new Error('MediaCapabilities unavailable');
        const info = await deadline(navigator.mediaCapabilities.decodingInfo({type: mode, ...config}));
        row.queries.push({mode, elapsedMs: performance.now() - start, supported: info.supported,
          smooth: info.smooth, powerEfficient: info.powerEfficient});
      } catch (error) { row.queries.push({mode, elapsedMs: performance.now() - start, error: String(error)}); }
    }
    results.push(row);
  }
  return {results, userAgent: navigator.userAgent, decoderOrSpeedProved: false, hdrRenderingProved: false,
    limits: 'Declarations only. powerEfficient is not proof of hardware. Pair actual bitstream/profile, VT session/output format, browser playback and unprofiled CPU/cadence. No user preferences changed.'};
};
