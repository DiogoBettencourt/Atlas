// What happens after the Ink app is done. Ink's exit() only unmounts the React
// tree; it does not end the process, and the terminal's stdin (kept in raw
// mode for typing) keeps Node's event loop alive, so without an explicit
// process exit /exit and /quit left the CLI hanging instead of returning to
// the shell (#28).

// Waits for the app to finish, then ends the process: 0 for a normal exit
// (including /exit and Ctrl+C), 1 if the app crashed. `endProcess` is
// process.exit in production; process.exit() runs the "exit" handler, which is
// what puts the terminal back and stops a server the CLI started.
export async function exitWhenDone(
  waitUntilExit: () => Promise<unknown>,
  endProcess: (code: number) => void,
  reportError: (message: string) => void = () => {}
): Promise<void> {
  try {
    await waitUntilExit();
  } catch (err) {
    reportError(err instanceof Error ? err.message : String(err));
    endProcess(1);
    return;
  }
  endProcess(0);
}
