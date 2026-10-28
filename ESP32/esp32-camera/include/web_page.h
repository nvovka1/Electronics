/*
 * The single HTML page served at "/". The video itself comes from the stream
 * server on port 81, so the page stays responsive while a stream is open.
 */
#pragma once

#include <pgmspace.h>

static const char IndexHtml[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Camera</title>
<style>
  :root { color-scheme: dark; }
  body { margin: 0; font-family: system-ui, sans-serif; background: #111; color: #eee; }
  header { padding: 12px 16px; display: flex; align-items: center; gap: 12px; }
  h1 { font-size: 18px; margin: 0; flex: 1; }
  #status { font-size: 13px; color: #8c8; }
  main { display: flex; flex-wrap: wrap; gap: 16px; padding: 0 16px 16px; }
  #view { flex: 1 1 480px; min-width: 0; background: #000; border-radius: 6px;
          display: flex; align-items: center; justify-content: center; }
  #stream { max-width: 100%; height: auto; display: block; }
  aside { flex: 0 1 260px; display: flex; flex-direction: column; gap: 12px; }
  label { display: flex; flex-direction: column; gap: 4px; font-size: 13px; }
  .row { flex-direction: row; align-items: center; gap: 8px; }
  select, input[type=range] { width: 100%; }
  button, a.button { padding: 8px 12px; border: 0; border-radius: 4px; background: #2a6;
          color: #fff; font-size: 14px; cursor: pointer; text-align: center; text-decoration: none; }
  button.stop { background: #a33; }
</style>
</head>
<body>
<header>
  <h1>ESP32 Camera</h1>
  <span id="status">connecting...</span>
</header>
<main>
  <div id="view"><img id="stream" alt="camera stream"></div>
  <aside>
    <button id="toggle" class="stop">Stop stream</button>
    <a class="button" href="/capture" target="_blank">Snapshot (JPEG)</a>
    <label>Resolution
      <select data-var="framesize">
        <option value="13">UXGA 1600x1200</option>
        <option value="10">XGA 1024x768</option>
        <option value="9">SVGA 800x600</option>
        <option value="8">VGA 640x480</option>
        <option value="6">CIF 400x296</option>
        <option value="5">QVGA 320x240</option>
      </select>
    </label>
    <label>Quality (lower = better, slower)
      <input type="range" min="10" max="63" data-var="quality">
    </label>
    <label>Brightness
      <input type="range" min="-2" max="2" data-var="brightness">
    </label>
    <label>Contrast
      <input type="range" min="-2" max="2" data-var="contrast">
    </label>
    <label>Flash LED
      <input type="range" min="0" max="255" data-var="flash">
    </label>
    <label class="row"><input type="checkbox" data-var="vflip"> Flip vertical</label>
    <label class="row"><input type="checkbox" data-var="hmirror"> Mirror horizontal</label>
  </aside>
</main>
<script>
  const streamUrl = `http://${location.hostname}:81/stream`;
  const img = document.getElementById('stream');
  const toggle = document.getElementById('toggle');
  const status = document.getElementById('status');
  let streaming = false;

  function startStream() {
    img.src = `${streamUrl}?t=${Date.now()}`;
    streaming = true;
    toggle.textContent = 'Stop stream';
    toggle.className = 'stop';
  }

  function stopStream() {
    // Clearing src closes the connection so the camera frees the stream slot.
    img.src = '';
    streaming = false;
    toggle.textContent = 'Start stream';
    toggle.className = '';
  }

  toggle.onclick = () => (streaming ? stopStream() : startStream());
  img.onload = () => { status.textContent = 'live'; status.style.color = '#8c8'; };
  img.onerror = () => {
    if (!streaming) return;
    status.textContent = 'stream lost - retrying';
    status.style.color = '#c88';
    setTimeout(() => streaming && startStream(), 2000);
  };

  function sendControl(name, value) {
    return fetch(`/control?var=${name}&val=${value}`);
  }

  async function loadStatus() {
    const settings = await (await fetch('/status')).json();
    document.querySelectorAll('[data-var]').forEach(el => {
      const value = settings[el.dataset.var];
      if (value === undefined) return;
      if (el.type === 'checkbox') el.checked = value === 1;
      else el.value = value;
    });
  }

  document.querySelectorAll('[data-var]').forEach(el => {
    el.addEventListener('change', async () => {
      const value = el.type === 'checkbox' ? (el.checked ? 1 : 0) : el.value;
      // A resolution change mid-stream can stall the open connection; restart it.
      const restart = el.dataset.var === 'framesize' && streaming;
      if (restart) stopStream();
      await sendControl(el.dataset.var, value);
      if (restart) startStream();
    });
  });

  loadStatus().catch(() => {}).finally(startStream);
</script>
</body>
</html>
)rawliteral";
