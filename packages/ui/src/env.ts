// Where the Atlas API lives. Served by Atlas itself (the normal case), the
// API is on the same origin as the page. In `vite dev` the page is on :5173
// and the API elsewhere, so default to Atlas's own default port, overridable
// with VITE_ATLAS_URL or a ?server=<url> query parameter.
export function serverUrl(): string {
  const fromQuery = new URLSearchParams(window.location.search).get("server");
  if (fromQuery) return fromQuery;
  if (import.meta.env.DEV) {
    return (import.meta.env.VITE_ATLAS_URL as string | undefined) ?? "http://127.0.0.1:8080";
  }
  return window.location.origin;
}
