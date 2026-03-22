const char *configPageHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Settings</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta charset="UTF-8">

    <style>
        :root{
            --bg:#0f1724;
            --panel:#182235;
            --panel2:#111927;
            --text:#ffffff;
            --muted:#b9c2d0;
            --accent:#2f7df6;
            --accent2:#1459c7;
            --danger:#c0392b;
            --dangerSoft:#3a1f1f;
            --border:rgba(255,255,255,0.08);
            --inputBg:#eef2f7;
            --inputText:#111;
        }

        * { box-sizing: border-box; }

        body {
            margin: 0;
            background: linear-gradient(180deg,#0d1a2e 0%, #08111d 100%);
            color: var(--text);
            font-family: Arial, sans-serif;
        }

        .page {
            max-width: 560px;
            margin: 0 auto;
            padding: 16px;
        }

        .title-card,
        form {
            background: rgba(255,255,255,0.06);
            border: 1px solid var(--border);
            border-radius: 18px;
            box-shadow: 0 8px 24px rgba(0,0,0,0.25);
        }

        .title-card {
            padding: 16px;
            margin-bottom: 14px;
            text-align: center;
        }

        .title-card h2 {
            margin: 0;
            font-size: 30px;
            letter-spacing: 0.3px;
        }

        .title-card p {
            margin: 8px 0 0 0;
            color: var(--muted);
            font-size: 14px;
        }

        form {
            padding: 16px;
        }

        .section {
            background: rgba(255,255,255,0.03);
            border: 1px solid rgba(255,255,255,0.05);
            border-radius: 16px;
            padding: 14px;
            margin-bottom: 14px;
        }

        .section h3 {
            margin: 0 0 12px 0;
            font-size: 18px;
            color: #fff;
        }

        .input-container {
            display: flex;
            flex-direction: column;
            gap: 6px;
            margin-bottom: 12px;
            text-align: left;
        }

        .input-container:last-child {
            margin-bottom: 0;
        }

        label {
            font-size: 15px;
            color: var(--muted);
            font-weight: bold;
        }

        input, select {
            width: 100%;
            min-height: 48px;
            border: none;
            border-radius: 12px;
            padding: 10px 12px;
            font-size: 17px;
            background: var(--inputBg);
            color: var(--inputText);
        }
        input[type="number"] {
            width: 90px;
            text-align: center;
            flex-shrink: 0;
        }

        .input-container:has(input[type="number"]) {
            flex-direction: row;
            align-items: center;
            justify-content: space-between;
            gap: 12px;
        }

        .input-container:has(input[type="number"]) label {
            margin: 0;
            flex: 1;
        }

        input:disabled, select:disabled {
            opacity: 0.6;
        }

        .checkbox-row {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-bottom: 12px;
            text-align: left;
        }

        .checkbox-row:last-child {
            margin-bottom: 0;
        }

        .checkbox-row input[type="checkbox"] {
            width: 20px;
            height: 20px;
            min-height: auto;
            margin: 0;
        }

        .checkbox-row label {
            margin: 0;
            color: #fff;
            font-weight: normal;
        }

        .inline-row {
            display: grid;
            grid-template-columns: 1fr 110px;
            gap: 10px;
            align-items: end;
        }

        .critical {
            border: 1px solid rgba(255,120,120,0.25);
            background: rgba(192,57,43,0.08);
            border-radius: 14px;
            padding: 12px;
        }

        .critical label {
            color: #ffb3b3;
        }

        .critical-note {
            font-size: 12px;
            color: #ffb3b3;
            margin: 4px 0 0 0;
            line-height: 1.35;
        }

        .helper {
            font-size: 12px;
            color: var(--muted);
            line-height: 1.35;
            margin-top: 6px;
        }

        .actions {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
            margin-top: 8px;
        }

        .primary-btn, .secondary-btn, .danger-btn {
            appearance: none;
            border: none;
            border-radius: 14px;
            min-height: 56px;
            font-size: 18px;
            font-weight: bold;
            color: white;
            cursor: pointer;
            width: 100%;
        }

        .primary-btn {
            background: linear-gradient(180deg,var(--accent),var(--accent2));
        }

        .secondary-btn {
            background: linear-gradient(180deg,#303846,#151b24);
        }

        .danger-btn {
            background: linear-gradient(180deg,#d14b3f,#a52a1f);
        }

        .system-actions {
            display: grid;
            grid-template-columns: 1fr;
            gap: 10px;
            margin-top: 10px;
        }

        .label-disabled { opacity: 0.5; }
        .label-enabled  { opacity: 1; }

        @media screen and (max-width: 560px) {
            .page { padding: 12px; }
            .title-card h2 { font-size: 26px; }
            .actions { grid-template-columns: 1fr; }
        }
    </style>
</head>

<body>
<div class="page">

    <div class="title-card">
        <h2>Sensor Configuration</h2>
        <title>Pressure Sensor Settings</title>
    </div>

    <form action="/submit" method="post">

        <!-- ── Sensor 1 ──────────────────────────────────────────── -->
        <div class="section">
            <h3>Sensor 1 (Low Pressure)</h3>

            <div class="input-container">
                <label for="maxPressure1">Max Pressure (0-150 bar)</label>
                <input type="number" id="maxPressure1" name="maxPressure1"
                       value="{maxPressure1}" min="0" max="150" step="1">
            </div>

            <div class="input-container">
                <label for="minPressure1">Min Pressure (0-2 bar)</label>
                <input type="number" id="minPressure1" name="minPressure1"
                       value="{minPressure1}" min="0" max="2" step="0.1">
            </div>

            <div class="input-container">
                <label for="minVdc1">Min Voltage (0-1 VDC)</label>
                <input type="number" id="minVdc1" name="minVdc1"
                       value="{minVdc1}" min="0" max="1" step="0.1">
            </div>

            <div class="input-container">
                <label for="maxVdc1">Max Voltage (0-5 VDC)</label>
                <input type="number" id="maxVdc1" name="maxVdc1"
                       value="{maxVdc1}" min="0" max="5" step="0.01">
            </div>
        </div>

        <!-- ── Sensor 2 ──────────────────────────────────────────── -->
        <div class="section">
            <h3>Sensor 2 (High Pressure)</h3>

            <div class="input-container">
                <label for="maxPressure2">Max Pressure (0-150 bar)</label>
                <input type="number" id="maxPressure2" name="maxPressure2"
                       value="{maxPressure2}" min="0" max="150" step="1">
            </div>

            <div class="input-container">
                <label for="minPressure2">Min Pressure (0-2 bar)</label>
                <input type="number" id="minPressure2" name="minPressure2"
                       value="{minPressure2}" min="0" max="2" step="0.1">
            </div>

            <div class="input-container">
                <label for="minVdc2">Min Voltage (0-1 VDC)</label>
                <input type="number" id="minVdc2" name="minVdc2"
                       value="{minVdc2}" min="0" max="1" step="0.1">
            </div>

            <div class="input-container">
                <label for="maxVdc2">Max Voltage (0-5 VDC)</label>
                <input type="number" id="maxVdc2" name="maxVdc2"
                       value="{maxVdc2}" min="0" max="5" step="0.01">
            </div>
        </div>

        <!-- ── Network ───────────────────────────────────────── -->
        <div class="section">
            <h3>Network</h3>

            <div class="input-container">
                <label for="modo">WiFi mode</label>
                <select name="modo" id="modo" onchange="toggleAPPassword(this)">
                    <option value="0" {wifiMode0}>AP only</option>
                    <option value="1" {wifiMode1}>Client</option>
                </select>
            </div>

            <div class="input-container">
                <label for="sensorMode">Sensor source</label>
                <select name="sensorMode" id="sensorMode">
                    <option value="0" {sensorMode0}>Real</option>
                    <option value="1" {sensorMode1}>Demo</option>
                    <option value="2" {sensorMode2}>Demo + UDP</option>
                </select>
            </div>
            <p class="helper" style="margin-top:-6px;">
                Real only sends UDP when ADS1115 is detected. Demo + UDP keeps the same pipeline and transmits simulated values.
            </p>

            <div class="input-container">
                <label for="APpassword" id="apPassLabel">AP password</label>
                <input type="password" id="APpassword" name="APpassword"
                       value="{APpassword}" minlength="8" maxlength="19">
            </div>

            <div class="input-container">
                <label for="signalkMaxAttempts" id="skAttLabel">SignalK discovery attempts (0 = unlimited)</label>
                <input type="number" id="signalkMaxAttempts" name="signalkMaxAttempts"
                       value="{signalkMaxAttempts}" min="0" max="60">
            </div>
            <p class="helper" style="margin-top:-6px;">
                Each attempt is 5 s apart. 12 = ~1 min, 0 = keep trying forever.
                Only active in Client mode.
            </p>

            <div class="input-container" style="margin-top:12px;">
                <label for="outPort" id="outPortLabel">SignalK UDP port (1024-65535)</label>
                <input type="number" id="outPort" name="outPort"
                       value="{outPort}" min="1024" max="65535">
            </div>
            <p class="helper" style="margin-top:-6px;">
                Default: 4210. Only active in Client mode.
            </p>

            <div class="input-container" style="margin-top:12px;">
                <label for="signalkIp" id="signalkIpLabel">SignalK server IP</label>
                <input type="text" id="signalkIp" name="signalkIp"
                       value="{signalkIp}" placeholder="0.0.0.0" maxlength="15">
            </div>
            <p class="helper" style="margin-top:-6px;">
                Discovered automatically. Clear or set 0.0.0.0 to force rediscovery. Only active in Client mode.
            </p>
        </div>

        <!-- ── Maintenance ───────────────────────────────────── -->
        <div class="section">
            <h3>Maintenance</h3>

            <div class="system-actions">
                <button type="button" class="secondary-btn"
                        onclick="window.open('/update','_blank')">
                    Update firmware (OTA)
                </button>

                <button type="button" class="secondary-btn"
                        onclick="window.open('/updatefs','_blank')">
                    Update filesystem
                </button>

                <button type="button" class="secondary-btn"
                        onclick="window.location='/tools'">
                    Tools
                </button>

                <button type="button" class="danger-btn"
                        onclick="if(confirm('Restore factory settings?')) window.location.href='/factory'">
                    Factory reset
                </button>
            </div>

            <div class="helper">
                Use factory reset only if you want to restore the original defaults.
            </div>
        </div>

        <div class="actions">
            <button type="button" class="secondary-btn" onclick="window.location='/'">Back</button>
            <button type="submit" class="primary-btn">Save settings</button>
        </div>

    </form>
</div>

<script>
    function toggleAPPassword(select) {
        var inp     = document.getElementById('APpassword');
        var lbl     = document.getElementById('apPassLabel');
        var skInp   = document.getElementById('signalkMaxAttempts');
        var skLbl   = document.getElementById('skAttLabel');
        var portInp = document.getElementById('outPort');
        var portLbl = document.getElementById('outPortLabel');
        var ipInp   = document.getElementById('signalkIp');
        var ipLbl   = document.getElementById('signalkIpLabel');
        var isAP    = (select.value === '0');

        // AP password: only editable in AP mode
        inp.disabled = !isAP;
        lbl.classList.toggle('label-disabled', !isAP);
        lbl.classList.toggle('label-enabled',   isAP);

        // SignalK fields: only active in Client mode
        skInp.disabled  = isAP;
        skLbl.classList.toggle('label-disabled', isAP);
        portInp.disabled = isAP;
        portLbl.classList.toggle('label-disabled', isAP);
        ipInp.disabled  = isAP;
        ipLbl.classList.toggle('label-disabled', isAP);
    }

    window.onload = function() {
        toggleAPPassword(document.getElementById('modo'));
    };
</script>

</body>
</html>
)rawliteral";
