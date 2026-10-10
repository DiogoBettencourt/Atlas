import { fileURLToPath } from "node:url";
import { defineConfig } from "vitest/config";

export default defineConfig({
  resolve: {
    alias: {
      // The shared API client, consumed as source (see packages/client).
      "@atlas/client": fileURLToPath(new URL("../client/src/index.ts", import.meta.url)),
    },
  },
  test: {
    // Without this, vitest's default glob also picks up the compiled
    // copies of these same test files under dist/ once `npm run build`
    // has run, and every test executes twice.
    exclude: ["**/node_modules/**", "**/dist/**"],
  },
});
