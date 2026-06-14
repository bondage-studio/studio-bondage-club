/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_PLATFORM?: "android" | "web";
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
