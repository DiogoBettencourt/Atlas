#!/usr/bin/env node
// Test fixture standing in for the real atlas server binary: stays
// alive until killed, so launch.test.ts can poll a fake checkHealth()
// against it without the process exiting out from under the test.
setInterval(() => {}, 1000);
