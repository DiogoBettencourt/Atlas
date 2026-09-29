#!/usr/bin/env node
// Test fixture standing in for a broken atlas binary: exits right away
// with a nonzero code, so launch.test.ts can assert launchServer()
// surfaces this as an early failure instead of waiting out the full
// poll timeout.
process.exit(1);
