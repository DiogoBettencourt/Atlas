import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    // e2e/ needs the real atlas binary: run it with `npm run test:e2e`.
    exclude: ["**/node_modules/**", "**/dist/**", "e2e/**"],
  },
});
