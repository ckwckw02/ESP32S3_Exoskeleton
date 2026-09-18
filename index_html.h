#ifndef INDEX_HTML_H
#define INDEX_HTML_H

// Simple control page served by the ESP32-S3 access point.
static const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Motor Control</title>
  <style>
    body { font-family: Arial, Helvetica, sans-serif; max-width: 480px; margin: 20px auto; padding: 0 16px; background: #f7f7f7; color: #222; }
    h1 { font-size: 1.4em; text-align: center; }
    .section { margin: 16px 0; padding: 14px; background: #fff; border: 1px solid #e0e0e0; border-radius: 10px; }
    .section h2 { font-size: 1.05em; margin: 0 0 10px; color: #444; }
    button { padding: 10px 14px; margin: 4px; font-size: 1em; border: none; border-radius: 8px; cursor: pointer; color: #fff; }
    .start { background: #28a745; }
    .stop  { background: #dc3545; }
    .origin{ background: #007bff; }
    .set   { background: #ffc107; color: #212529; }
    label { display: block; margin: 10px 0 3px; font-weight: bold; }
    input[type=number] { width: 100%; padding: 9px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 6px; }
    .status { margin-top: 14px; padding: 12px; background: #fff; border: 1px solid #e0e0e0; border-radius: 10px; font-family: monospace; white-space: pre-wrap; font-size: 0.9em; }
  </style>
</head>
<body>
  <h1>Motor Control</h1>

  <div class="section">
    <h2>Run</h2>
    <button class="start" onclick="cmd('/start')">START</button>
    <button class="stop" onclick="cmd('/stop')">STOP</button>
  </div>

  <div class="section">
    <h2>Control Mode</h2>
    <label style="font-weight:normal; display:inline;">
      <input type="checkbox" id="speed_only" onchange="setMode(this.checked)">
      Speed-only loop (comm_can_set_rpm) &mdash; off = pos+spd loop (comm_can_set_pos_spd)
    </label>
  </div>

  <div class="section">
    <h2>Origin</h2>
    <button class="origin" onclick="cmd('/set_origin_left')">Set LEFT motor origin</button>
    <button class="origin" onclick="cmd('/set_origin_right')">Set RIGHT motor origin</button>
  </div>

  <div class="section">
    <h2>Motor Parameters</h2>
    <form onsubmit="return setPara(event)">
      <p style="margin:4px 0; font-weight:bold;">To Pos (R&rarr;Pos, L&rarr;Pos)</p>
      <label id="lbl_pos">Pos (deg) &mdash; pos+spd mode only</label>
      <input type="number" step="any" name="pos" id="pos" value="180">
      <label>Spd (-40000..40000)</label>
      <input type="number" name="spd_pos" id="spd_pos" value="2000" min="-40000" max="40000">
      <label id="lbl_rpa_pos">RPA (0..60000) &mdash; pos+spd mode only</label>
      <input type="number" name="rpa_pos" id="rpa_pos" value="2000" min="0" max="60000">
      <label>Approach Pos time (ms)</label>
      <input type="number" name="approach_pos" id="approach_pos" value="3000" min="50">

      <p style="margin:12px 0 4px; font-weight:bold;">To Zero (R&rarr;0, L&rarr;0)</p>
      <label>Spd (-40000..40000)</label>
      <input type="number" name="spd_zero" id="spd_zero" value="2000" min="-40000" max="40000">
      <label id="lbl_rpa_zero">RPA (0..60000) &mdash; pos+spd mode only</label>
      <input type="number" name="rpa_zero" id="rpa_zero" value="2000" min="0" max="60000">
      <label>Approach Zero time (ms)</label>
      <input type="number" name="approach_zero" id="approach_zero" value="3000" min="50">

      <p style="margin:12px 0 4px; font-weight:bold;">Shared</p>
      <label>Cooldown (ms)</label>
      <input type="number" name="cooldown" id="cooldown" value="500" min="0">
      <button class="set" type="submit">Set</button>
    </form>
  </div>

  <div class="status" id="status">Status: loading...</div>

  <script>
    function cmd(path) {
      fetch(path).catch(function () {});
    }
    function setMode(on) {
      fetch('/set_mode?mode=' + (on ? 1 : 0)).catch(function () {});
    }
    // All parameter field ids, in send order. Fields hidden for the current
    // mode are skipped so their stored values persist on the device.
    var PARA_IDS = ['pos', 'spd_pos', 'rpa_pos', 'approach_pos',
                    'spd_zero', 'rpa_zero', 'approach_zero', 'cooldown'];
    function setPara(e) {
      e.preventDefault();
      var q = '';
      for (var i = 0; i < PARA_IDS.length; i++) {
        var el = document.getElementById(PARA_IDS[i]);
        if (!el || el.style.display === 'none') continue; // hidden in this mode
        q += (q ? '&' : '') + PARA_IDS[i] + '=' + encodeURIComponent(el.value);
      }
      fetch('/set_para?' + q).catch(function () {});
      return false;
    }
    // Show/hide the fields that don't apply to the current control mode.
    function applyModeUI(rpmOn) {
      var posOnly = ['pos', 'rpa_pos', 'rpa_zero']; // hidden in speed-only mode
      for (var i = 0; i < posOnly.length; i++) {
        var el = document.getElementById(posOnly[i]);
        if (el) el.style.display = rpmOn ? 'none' : '';
      }
    }
    // Stop auto-filling the inputs once the user starts editing them.
    var paramsTouched = false;
    for (var i = 0; i < PARA_IDS.length; i++) {
      var el = document.getElementById(PARA_IDS[i]);
      if (el) el.addEventListener('input', function () { paramsTouched = true; });
    }

    setInterval(function () {
      fetch('/status')
        .then(function (r) { return r.text(); })
        .then(function (t) {
          document.getElementById('status').textContent = t;
          // Keep the mode checkbox + visible fields in sync with the device.
          var cb = document.getElementById('speed_only');
          if (cb) {
            var on = t.indexOf('mode: RPM') >= 0;
            if (cb.checked !== on) cb.checked = on;
            applyModeUI(on);
          }
          // Auto-populate the parameter inputs from the device until first edit.
          if (!paramsTouched) {
            for (var j = 0; j < PARA_IDS.length; j++) {
              var m = t.match(new RegExp('^' + PARA_IDS[j] + '=(-?[0-9.]+)', 'm'));
              if (m) document.getElementById(PARA_IDS[j]).value = parseFloat(m[1]);
            }
          }
        })
        .catch(function () {});
    }, 100);
  </script>
</body>
</html>
)rawliteral";

#endif
