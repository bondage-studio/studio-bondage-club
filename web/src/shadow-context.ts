import { createContext, useContext } from "react";

export const ShadowContext = createContext<HTMLElement | null>(null);

export function useShadowContainer(): HTMLElement | null {
  return useContext(ShadowContext);
}
