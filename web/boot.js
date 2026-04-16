/*
 * Boot loader for Ruby on Bare Metal browser demo.
 * Based on ktock/qemu-wasm-demo.
 * SPDX-License-Identifier: GPL-2.0
 */
import 'https://cdn.jsdelivr.net/npm/@xterm/xterm@5.5.0/lib/xterm.js';
import 'https://unpkg.com/xterm-pty/index.js';
import initEmscriptenModule from './qemu-system-x86_64';

var packFiles = [
  { url: "pack/kernel.elf",         path: "/pack/kernel.elf" },
  { url: "pack/bios-256k.bin",      path: "/pack/bios-256k.bin" },
  { url: "pack/kvmvapic.bin",       path: "/pack/kvmvapic.bin" },
  { url: "pack/linuxboot_dma.bin",  path: "/pack/linuxboot_dma.bin" },
  { url: "pack/multiboot_dma.bin",  path: "/pack/multiboot_dma.bin" },
  { url: "pack/vgabios-stdvga.bin", path: "/pack/vgabios-stdvga.bin" }
];

var statusEl = document.getElementById("status");
function setStatus(msg) {
  if (statusEl) statusEl.textContent = msg;
}

window.startBoot = async function () {
  var btn = document.getElementById("start-btn");
  btn.disabled = true;

  if (!window.crossOriginIsolated) {
    setStatus("Error: Cross-origin isolation is not enabled. SharedArrayBuffer is unavailable.");
    return;
  }

  // Set up xterm.js
  var term = new Terminal({
    cols: 80,
    rows: 30,
    fontSize: 14,
    fontFamily: "'Courier New', monospace",
    theme: { background: "#000000", foreground: "#cccccc", cursor: "#ffffff" }
  });

  var container = document.getElementById("terminal-container");
  container.style.display = "block";
  document.getElementById("start-screen").style.display = "none";
  term.open(container);

  // Create PTY pair via xterm-pty
  var pty = openpty();
  term.loadAddon(pty.master);

  term.writeln("Loading files...\r\n");
  setStatus("Downloading kernel and BIOS files...");

  // Fetch all pack files
  var fetched = [];
  try {
    fetched = await Promise.all(packFiles.map(async function (f) {
      var resp = await fetch(f.url);
      if (!resp.ok) throw new Error(f.url + ": " + resp.status);
      var buf = await resp.arrayBuffer();
      term.writeln("  Loaded " + f.url + " (" + Math.round(buf.byteLength / 1024) + " KB)");
      return { path: f.path, data: new Uint8Array(buf) };
    }));
  } catch (err) {
    setStatus("Error loading files: " + err.message);
    return;
  }

  term.writeln("\r\nStarting QEMU...\r\n");
  setStatus("Starting QEMU (this may take a moment)...");

  // Configure Emscripten Module
  var Module = {
    arguments: [
      "-nographic",
      "-machine", "q35",
      "-m", "512M",
      "-L", "/pack/",
      "-kernel", "/pack/kernel.elf",
      "-nic", "none"
    ],
    pty: pty.slave,
    preRun: [
      function (mod) {
        try { mod.FS.mkdir("/pack"); } catch (e) { /* exists */ }
        for (var i = 0; i < fetched.length; i++) {
          mod.FS.writeFile(fetched[i].path, fetched[i].data);
        }
        console.log("preRun: wrote " + fetched.length + " files to FS");
      }
    ]
  };

  try {
    var instance = await initEmscriptenModule(Module);

    // Patch TTY poll for xterm-pty compatibility
    var oldPoll = Module.TTY.stream_ops.poll;
    Module.TTY.stream_ops.poll = function (stream, timeout) {
      if (!pty.slave.readable) {
        return (pty.slave.readable ? 1 : 0) | (pty.slave.writable ? 4 : 0);
      }
      return oldPoll.call(this, stream, timeout);
    };
  } catch (err) {
    setStatus("QEMU error: " + err.message);
    console.error("QEMU error:", err);
  }
};
