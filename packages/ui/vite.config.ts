import react from "@vitejs/plugin-react";
import { fileURLToPath } from "node:url";
import { defineConfig } from "vitest/config";

export default defineConfig({
  plugins: [react()],
  // The Atlas backend serves the built app under /ui/ (see --ui-dir).
  base: "/ui/",
  resolve: {
    alias: {
      // The one AtlasClient, shared with AtlasCLI by source: it is plain
      // fetch code with no Node-only imports, so it runs in a browser as is.
      // Moving it to a published package is tracked in #19.
      "@atlas/client": fileURLToPath(new URL("../cli/src/api/client.ts", import.meta.url)),
    },
  },
  server: { port: 5173 },
  test: {
    environment: "jsdom",
    setupFiles: ["./src/test-setup.ts"],
    exclude: ["**/node_modules/**", "**/dist/**"],
  },
});
