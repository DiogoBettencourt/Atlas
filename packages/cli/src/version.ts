// The CLI's own version, read from package.json at runtime so there is no
// second hand-maintained string to drift. From both src/ (tsx) and dist/
// (built) the package manifest sits one directory up, and npm always
// includes package.json in the published tarball.
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);

export const VERSION: string = (require("../package.json") as { version: string }).version;
