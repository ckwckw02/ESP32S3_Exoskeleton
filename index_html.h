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
    .canerr { display: none; position: sticky; top: 0; z-index: 10; margin: 10px 0; padding: 14px; background: #dc3545; color: #fff; border-radius: 10px; font-weight: bold; text-align: center; font-size: 1.1em; box-shadow: 0 2px 8px rgba(0,0,0,.3); }
  </style>
</head>
<body>
  <h1>Motor Control 2026-10-22</h1>

  <div class="canerr" id="canerr">&#x96FB;&#x6A5F; CAN ERROR &#x2014; &#x63A7;&#x5236;&#x5DF2;&#x66AB;&#x505C;</div>

  <div class="section">
    <h2>Run</h2>
    <button class="start" onclick="cmd('/start')">START</button>
    <button class="stop" onclick="cmd('/stop')">STOP</button>
  </div>

  <div class="section">
    <h2>One-shot (pull &rarr; release)</h2>
    <button class="origin" onclick="cmd('/oneshot_left')">LEFT once</button>
    <button class="origin" onclick="cmd('/oneshot_right')">RIGHT once</button>
  </div>

  <div class="section">
    <h2>Control Mode</h2>
    <label style="font-weight:normal; display:inline;">
      <input type="radio" name="mode_sel" value="0" onchange="setMode(0)"> Position Velocity Loop
    </label><br>
    <label style="font-weight:normal; display:inline;">
      <input type="radio" name="mode_sel" value="1" onchange="setMode(1)"> Velocity Loop
    </label><br>
    <label style="font-weight:normal; display:inline;">
      <input type="radio" name="mode_sel" value="2" onchange="setMode(2)"> Equation
    </label>
  </div>

  <div class="section">
    <h2>Origin</h2>
    <button class="origin" onclick="cmd('/set_origin_left')">Set LEFT</button>
    <button class="origin" onclick="cmd('/set_origin_right')">Set RIGHT</button>
  </div>

  <div class="section">
    <h2>Tight (duty 0.3)</h2>
    <button class="start" onclick="cmd('/tight_start')">Start</button>
    <button class="stop" onclick="cmd('/tight_stop')">Stop</button>
  </div>

  <div class="section" id="para_section">
    <h2>Motor Parameters</h2>
    <form onsubmit="return setPara(event)">
      <p style="margin:4px 0; font-weight:bold;">To Pos (R&rarr;Pos, L&rarr;Pos)</p>
      <label id="lbl_pos">Pos (deg) &mdash; Position Velocity Loop only</label>
      <input type="number" step="any" name="pos" id="pos" value="360">
      <label>Spd (-40000..40000)</label>
      <input type="number" name="spd_pos" id="spd_pos" value="40000" min="-40000" max="40000">
      <label id="lbl_rpa_pos">RPA (0..60000) &mdash; Position Velocity Loop only</label>
      <input type="number" name="rpa_pos" id="rpa_pos" value="60000" min="0" max="60000">
      <label id="lbl_approach_pos">Lift time (ms) &mdash; Velocity Loop only</label>
      <input type="number" name="approach_pos" id="approach_pos" value="1000" min="50">

      <p style="margin:12px 0 4px; font-weight:bold;">To Zero (R&rarr;0, L&rarr;0)</p>
      <label>Spd (-40000..40000)</label>
      <input type="number" name="spd_zero" id="spd_zero" value="40000" min="-40000" max="40000">
      <label id="lbl_rpa_zero">RPA (0..60000) &mdash; Position Velocity Loop only</label>
      <input type="number" name="rpa_zero" id="rpa_zero" value="60000" min="0" max="60000">
      <label id="lbl_approach_zero">Release nominal time (ms) &mdash; Velocity Loop only</label>
      <input type="number" name="approach_zero" id="approach_zero" value="1000" min="50">

      <p style="margin:12px 0 4px; font-weight:bold;">Shared</p>
      <label>Cooldown (ms)</label>
      <input type="number" name="cooldown" id="cooldown" value="0" min="0">
      <button class="set" type="submit">Set</button>
    </form>
  </div>

  <div class="section" id="overlap_section">
    <h2>Overlap (Velocity Loop / Equation)</h2>
    <p style="margin:4px 0; font-size:0.9em; color:#666;">Opposite side's lift starts this long before the release's nominal end &mdash; both motors run together (one lift, one release). 0 = disabled.</p>
    <form onsubmit="return setOverlap(event)">
      <label>Overlap (ms)</label>
      <input type="number" name="overlap" id="overlap" value="100" min="0" max="5000">
      <button class="set" type="submit">Set</button>
    </form>
  </div>

  <div class="section" id="eq_section">
    <h2>Equation Mode</h2>
    <form onsubmit="return setEq(event)">
      <label>T (s)</label>
      <input type="number" step="any" name="eq_T" id="eq_T" value="1.78" min="0">
      <label>SL (cm)</label>
      <input type="number" step="any" name="eq_SL" id="eq_SL" value="49.4" min="0">
      <label>L (cm)</label>
      <input type="number" step="any" name="eq_L" id="eq_L" value="100" min="0">
      <label>Lp (cm)</label>
      <input type="number" step="any" name="eq_Lp" id="eq_Lp" value="10" min="0">
      <label>LD (cm)</label>
      <input type="number" step="any" name="eq_LD" id="eq_LD" value="52" min="0">
      <label>H (cm)</label>
      <input type="number" step="any" name="eq_H" id="eq_H" value="12" min="0">
      <label style="font-weight:normal; display:inline;">
        <input type="checkbox" id="eq_pd"> PD
      </label>
      <button class="set" type="submit">Set</button>
    </form>
  </div>

  <div class="section">
    <h2>Wi-Fi AP clients (debug)</h2>
    <div class="status" id="wifi" style="margin-top:0;">loading...</div>
  </div>

  <div class="status" id="status">Status: loading...</div>

  <script>
    function cmd(path) {
      fetch(path).catch(function () {});
    }
    // mode: 0 = pos+spd, 1 = speed-only (RPM), 2 = equation. Exactly one active.
    function setMode(m) {
      fetch('/set_mode?mode=' + m).catch(function () {});
    }
    // All parameter field ids, in send order. Fields hidden for the current
    // mode are skipped so their stored values persist on the device.
    var PARA_IDS = ['pos', 'spd_pos', 'rpa_pos', 'approach_pos',
                    'spd_zero', 'rpa_zero', 'approach_zero', 'cooldown'];
    // Equation-mode field ids (sent by setEq, auto-populated from /status).
    var EQ_IDS = ['eq_T', 'eq_SL', 'eq_L', 'eq_Lp', 'eq_LD', 'eq_H'];
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
    // Send the equation-mode inputs to the device.
    function setEq(e) {
      e.preventDefault();
      var q = '';
      for (var i = 0; i < EQ_IDS.length; i++) {
        var el = document.getElementById(EQ_IDS[i]);
        if (!el) continue;
        q += (q ? '&' : '') + EQ_IDS[i] + '=' + encodeURIComponent(el.value);
      }
      var pdEl = document.getElementById('eq_pd');
      q += '&eq_pd=' + (pdEl && pdEl.checked ? 1 : 0);
      fetch('/set_para?' + q).catch(function () {});
      return false;
    }
    // Send the overlap window (velocity-loop modes) to the device.
    function setOverlap(e) {
      e.preventDefault();
      var el = document.getElementById('overlap');
      if (el) fetch('/set_para?overlap=' + encodeURIComponent(el.value)).catch(function () {});
      return false;
    }
    // Show/hide the sections and fields that don't apply to this mode.
    // modeId: 0 = pos+spd (pure position: no time fields, no overlap),
    // 1 = RPM (hide pos + RPA, show overlap),
    // 2 = equation (hide Motor Parameters, show Equation + overlap sections).
    function applyModeUI(modeId) {
      var paraSec = document.getElementById('para_section');
      if (paraSec) paraSec.style.display = (modeId === 2) ? 'none' : '';
      var eqSec = document.getElementById('eq_section');
      if (eqSec) eqSec.style.display = (modeId === 2) ? '' : 'none';
      var ovSec = document.getElementById('overlap_section');
      if (ovSec) ovSec.style.display = (modeId === 1 || modeId === 2) ? '' : 'none';
      // In RPM mode the pos + RPA fields don't apply; restore them otherwise.
      var rpmHide = [['pos','lbl_pos'], ['rpa_pos','lbl_rpa_pos'], ['rpa_zero','lbl_rpa_zero']];
      for (var i = 0; i < rpmHide.length; i++) {
        var el = document.getElementById(rpmHide[i][0]);
        if (el) el.style.display = (modeId === 1) ? 'none' : '';
        var lb = document.getElementById(rpmHide[i][1]);
        if (lb) lb.style.display = (modeId === 1) ? 'none' : '';
      }
      // In pos+spd mode the phase time fields don't apply (pure position
      // control); restore them in RPM mode.
      var spdHide = [['approach_pos','lbl_approach_pos'], ['approach_zero','lbl_approach_zero']];
      for (var j = 0; j < spdHide.length; j++) {
        var el2 = document.getElementById(spdHide[j][0]);
        if (el2) el2.style.display = (modeId === 0) ? 'none' : '';
        var lb2 = document.getElementById(spdHide[j][1]);
        if (lb2) lb2.style.display = (modeId === 0) ? 'none' : '';
      }
    }
    // Stop auto-filling the inputs once the user starts editing them.
    var paramsTouched = false;
    for (var i = 0; i < PARA_IDS.length; i++) {
      var el = document.getElementById(PARA_IDS[i]);
      if (el) el.addEventListener('input', function () { paramsTouched = true; });
    }
    var eqTouched = false;
    for (var k = 0; k < EQ_IDS.length; k++) {
      var e2 = document.getElementById(EQ_IDS[k]);
      if (e2) e2.addEventListener('input', function () { eqTouched = true; });
    }
    // The pd checkbox has no 'input' event to watch — a toggle IS the edit,
    // so mark it touched immediately or auto-populate would re-check it.
    var pdCb0 = document.getElementById('eq_pd');
    if (pdCb0) pdCb0.addEventListener('change', function () { eqTouched = true; });
    // Overlap field: stop auto-filling once edited.
    var overlapTouched = false;
    var ovEl0 = document.getElementById('overlap');
    if (ovEl0) ovEl0.addEventListener('input', function () { overlapTouched = true; });

    setInterval(function () {
      fetch('/status')
        .then(function (r) { return r.text(); })
        .then(function (t) {
          document.getElementById('status').textContent = t;
          // CAN watchdog banner: the /status body starts with the warning
          // while feedback is missing.
          var banner = document.getElementById('canerr');
          if (banner) banner.style.display = (t.indexOf('CAN ERROR') >= 0) ? 'block' : 'none';
          // Keep the mode selector + visible sections in sync with the device.
          var mId = t.match(/^mode_id=(\d+)$/m);
          if (mId) {
            var id = parseInt(mId[1], 10);
            var sel = document.querySelector('input[name="mode_sel"][value="' + id + '"]');
            if (sel && !sel.checked) sel.checked = true;
            applyModeUI(id);
          }
          // Auto-populate the parameter inputs from the device until first edit.
          if (!paramsTouched) {
            for (var j = 0; j < PARA_IDS.length; j++) {
              var m = t.match(new RegExp('^' + PARA_IDS[j] + '=(-?[0-9.]+)', 'm'));
              if (m) document.getElementById(PARA_IDS[j]).value = parseFloat(m[1]);
            }
          }
          // Auto-populate the overlap field from the device until first edit.
          if (!overlapTouched) {
            var mo = t.match(/^overlap=(\d+)/m);
            if (mo) { var ovEl = document.getElementById('overlap'); if (ovEl) ovEl.value = parseInt(mo[1], 10); }
          }
          // Auto-populate the equation inputs from the device until first edit.
          if (!eqTouched) {
            for (var k2 = 0; k2 < EQ_IDS.length; k2++) {
              var me = t.match(new RegExp('^' + EQ_IDS[k2] + '=(-?[0-9.]+)', 'm'));
              if (me) document.getElementById(EQ_IDS[k2]).value = parseFloat(me[1]);
            }
            var mpd = t.match(/^eq_pd=(\d+)$/m);
            if (mpd) {
              var pdEl2 = document.getElementById('eq_pd');
              if (pdEl2) pdEl2.checked = (parseInt(mpd[1], 10) !== 0);
            }
          }
        })
        .catch(function () {});
    }, 100);

    // Debug: refresh the AP client list once per second.
    setInterval(function () {
      fetch('/wifi')
        .then(function (r) { return r.text(); })
        .then(function (t) { document.getElementById('wifi').textContent = t; })
        .catch(function () {});
    }, 1000);
  </script>
</body>
</html>
)rawliteral";

#endif
