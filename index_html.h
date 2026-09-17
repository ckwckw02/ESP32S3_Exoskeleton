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
    <h2>Origin</h2>
    <button class="origin" onclick="cmd('/set_origin_left')">Set LEFT motor origin</button>
    <button class="origin" onclick="cmd('/set_origin_right')">Set RIGHT motor origin</button>
  </div>

  <div class="section">
    <h2>Motor Parameters</h2>
    <form onsubmit="return setPara(event)">
      <label>Pos (deg)</label>
      <input type="number" step="any" name="pos" id="pos" value="180">
      <label>Spd (-40000..40000)</label>
      <input type="number" name="spd" id="spd" value="2000" min="-40000" max="40000">
      <label>RPA (0..60000)</label>
      <input type="number" name="rpa" id="rpa" value="2000" min="0" max="60000">
      <label>Approach Pos time (ms)</label>
      <input type="number" name="approach_pos" id="approach_pos" value="3000" min="50">
      <label>Approach Zero time (ms)</label>
      <input type="number" name="approach_zero" id="approach_zero" value="3000" min="50">
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
    function setPara(e) {
      e.preventDefault();
      var pos = document.getElementById('pos').value;
      var spd = document.getElementById('spd').value;
      var rpa = document.getElementById('rpa').value;
      var ap = document.getElementById('approach_pos').value;
      var az = document.getElementById('approach_zero').value;
      var cd = document.getElementById('cooldown').value;
      fetch('/set_para?pos=' + pos + '&spd=' + spd + '&rpa=' + rpa +
            '&approach_pos=' + ap + '&approach_zero=' + az + '&cooldown=' + cd)
        .catch(function () {});
      return false;
    }
    setInterval(function () {
      fetch('/status')
        .then(function (r) { return r.text(); })
        .then(function (t) { document.getElementById('status').textContent = t; })
        .catch(function () {});
    }, 100);
  </script>
</body>
</html>
)rawliteral";

#endif
