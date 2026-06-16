// skipValidation — no-op appearance validation/sanitization on the hot path.

import { flags } from "../flags";
import type { Optimization } from "../optimization";

export const skipValidation: Optimization = {
  key: "skipValidation",
  install(mod) {
    mod.hookFunction("ValidationSanitizeEffects", 0, (args, next) =>
      flags.skipValidation ? false : next(args),
    );
    mod.hookFunction("ValidationSanitizeLock", 0, (args, next) =>
      flags.skipValidation ? false : next(args),
    );
  },
};
