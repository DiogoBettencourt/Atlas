import { defineConfig } from "vitest/config";

// End-to-end tests: they start the real atlas binary (ATLAS_BIN) against a
// stub Ollama, so they are not part of the plain `npm test`.
export default defineConfig({
  test: {
    include: ["e2e/**/*.e2e.test.ts"],
    testTimeout: 30_000,
    hookTimeout: 45_000,
  },
});
