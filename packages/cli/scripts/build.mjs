// Builds dist/cli.js, the file the `atlas` command runs.
//
// The CLI shares its API client with AtlasUI through the unpublished
// @atlas/client package (../client). Bundling that package into dist/cli.js
// keeps this npm package a single self-contained install; everything listed in
// "dependencies" stays an ordinary import that npm installs for the user.
import { build } from "esbuild";
import { chmodSync, readFileSync, rmSync } from "node:fs";

const pkg = JSON.parse(readFileSync(new URL("../package.json", import.meta.url), "utf8"));

rmSync("dist", { recursive: true, force: true });

await build({
  entryPoints: ["src/cli.ts"],
  outfile: "dist/cli.js",
  bundle: true,
  platform: "node",
  format: "esm",
  target: "node18",
  // "@atlas/client" is resolved through the "paths" entry in tsconfig.json.
  external: Object.keys(pkg.dependencies ?? {}),
  logLevel: "info",
});

// The shebang makes it runnable as a command; npm sets the bit on install,
// this covers running it straight from a clone.
chmodSync("dist/cli.js", 0o755);
