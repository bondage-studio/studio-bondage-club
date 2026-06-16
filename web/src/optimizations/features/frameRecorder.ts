export interface FrameSample {
  time: number;
  duration: number;
}

const frames: FrameSample[] = [];

export function getFrameSamples(): readonly FrameSample[] {
  return frames;
}

export function installFrameRecorder(mod: ModSDKModAPI): void {
  mod.hookFunction("DrawProcess", 20, (args, next) => {
    const t0 = performance.now();
    try {
      return next(args);
    } finally {
      frames.push({ time: Date.now(), duration: performance.now() - t0 });
    }
  });

  // Drop samples older than ~5 minutes.
  setInterval(() => {
    const cutoff = Date.now() - 305000;
    while (frames.length > 0 && frames[0].time < cutoff) frames.shift();
  }, 5000);
}