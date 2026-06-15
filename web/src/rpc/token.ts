// The /rpc capability token, held only in this module's closure.
//
// The backend embeds it in the bootstrap JSON; readStudioBootstrap() in
// originalPage.ts reads it at document-start, hands it here, and erases it from
// the parsed object + the DOM before any injected userscript runs. It is never
// attached to `window`, so co-resident untrusted code cannot read it — and
// without it, the backend closes the /rpc handshake (4401).

let token = "";

export function setRpcToken(value: string): void {
  token = value;
}

export function getRpcToken(): string {
  return token;
}
