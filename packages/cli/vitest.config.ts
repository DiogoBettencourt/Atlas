import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    // Without this, vitest's default glob also picks up the compiled
    // copies of these same test files under dist/ once `npm run build`
    // has run, and every test executes twice.
    exclude: ["**/node_modules/**", "**/dist/**"],
  },
});
