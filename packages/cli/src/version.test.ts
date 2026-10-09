import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { VERSION } from "./version.js";

const here = dirname(fileURLToPath(import.meta.url));

describe("version", () => {
  it("is a plain semver string", () => {
    expect(VERSION).toMatch(/^\d+\.\d+\.\d+$/);
  });

  // The backend and CLI share one project version (see VERSIONING.md).
  // Bumping one without the other fails here instead of shipping skew.
  it("matches the backend version in the root CMakeLists.txt", () => {
    const cmake = readFileSync(join(here, "..", "..", "..", "CMakeLists.txt"), "utf8");
    const match = /project\(\s*Atlas\s+VERSION\s+(\d+\.\d+\.\d+)/.exec(cmake);
    expect(match, "couldn't find project(Atlas VERSION ...) in CMakeLists.txt").not.toBeNull();
    expect(VERSION).toBe(match![1]);
  });
});
