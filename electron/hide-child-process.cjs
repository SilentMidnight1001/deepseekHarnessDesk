"use strict";

if (process.platform === "win32") {
  const childProcess = require("node:child_process");
  const patchMarker = Symbol.for("deepseekHarness.windowsHidePatch");

  function patchSpawnFunction(name) {
    const original = childProcess[name];
    if (typeof original !== "function" || original[patchMarker]) {
      return;
    }

    function patched(command, args, options) {
      if (Array.isArray(args)) {
        return original.call(this, command, args, {
          ...(options || {}),
          windowsHide: true,
        });
      }

      return original.call(this, command, {
        ...(args || {}),
        windowsHide: true,
      });
    }

    Object.defineProperty(patched, patchMarker, { value: true });
    childProcess[name] = patched;
  }

  patchSpawnFunction("spawn");
  patchSpawnFunction("spawnSync");
}
