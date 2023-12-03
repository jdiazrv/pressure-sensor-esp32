const char *configPageHTML = R"rawliteral(
<<!DOCTYPE html>
<html>

<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">
    <title>Sensor Configuration</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            font-size: 16px;
            background: #f4f4f4;
            margin: 0;
            padding: 0;
            display: flex;
            justify-content: center;
            align-items: center;
            height: auto;
        }

        .container {
            background: white;
            padding: 18px;
            border-radius: 8px;
            box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);
        }

        h1 {
            text-align: center;
            color: #333;
            font-size: 24px;
            margin-bottom: 6px 
        }

        .sensor-config {
        
        	margin-top: 5px;
            margin-bottom: 10px;
            font-size: 16px; /
        
        }

        .sensor-config h2 {
            color: #333;
            margin-bottom: 5px;
        }

        .sensor-config label {
            display: inline-block;
            width: 120px;
            text-align: right;
            margin-right: 10px;
        }

        .sensor-config input {
            width: 70px;
            padding: 4px;
            margin: 0 0 5px;
            font-size:14px;
        }

        input[type="submit"] {
            display: block;
            width: 100%;
            background-color: #1fa3ec;
            color: white;
            border: none;
            padding: 5px;
            margin-top: 15px;
            cursor: pointer;
            font-size: 16px;
        }
             #wifiMode {
            margin-bottom: 5px;
            font-size: 18px;
        }

        input[type="submit"]:hover {
            background-color: #0056b3;
        }

        /* Modificar el tamaño de la caja de texto del AP Password */
        #apPassword {
            width: 200px; /* Ajusta el ancho según tus necesidades */
            height: 40px; /* Ajusta la altura según tus necesidades */
        }
    </style>
    <script>
        function updateApPasswordState() {
            var wifiModeSelect = document.getElementById('wifiMode');
            var apPasswordInput = document.getElementById('apPassword');
            apPasswordInput.disabled = wifiModeSelect.value !== '0'; // 0 representa el modo AP
        }
        window.onload = function() {
            updateApPasswordState(); // Actualizar en la carga de la página
        };
    </script>
</head>

<body>
    <div class="container">
        <h1>Sensor Configuration</h1>
        <form action="/submit" method="post">
            <div class="sensor-config">
                <h2>Sensor 1</h2>
                <label for="maxPressure1">Max Pressure</label>
                <input type="number" id="maxPressure1" name="maxPressure1" value="{maxPressure1}" min="0.0" max="100.0"
                    step="1" placeholder="0-100"><br>
                <label for="minPressure1">Min Pressure</label>
                <input type="number" id="minPressure1" name="minPressure1" value="{minPressure1}" min="0.0" max="2"
                    step="1" placeholder="0-2"><br>
                <label for="minVdc1">Min VDC</label>
                <input type="number" id="minVdc1" name="minVdc1" value="{minVdc1}" min="0.0" max="1.0" step="0.1"
                    placeholder="0-1"><br>
                <label for="maxVdc1">Max VDC</label>
                <input type="number" id="maxVdc1" name="maxVdc1" value="{maxVdc1}" min="0.0" max="5.0" step="0.01"
                    placeholder="0-5">
            </div>
            <div class="sensor-config">
                <h2>Sensor 2</h2>
                <label for="maxPressure2">Max Pressure</label>
                <input type="number" id="maxPressure2" name="maxPressure2" value="{maxPressure2}" min="0.0" max="100.0"
                    step="1" placeholder="0-100"><br>
                <label for="minPressure2">Min Pressure</label>
                <input type="number" id="minPressure2" name="minPressure2" value="{minPressure2}" min="0.0" max="2.0"
                    step="1" placeholder="0-2"><br>
                <label for="minVdc2">Min VDC</label>
                <input type="number" id="minVdc2" name="minVdc2" value="{minVdc2}" min="0.0" max="1.0" step="0.1"
                    placeholder="0-1"><br>
                <label for="maxVdc2">Max VDC</label>
                <input type="number" id="maxVdc2" name="maxVdc2" value="{maxVdc2}" min="0.0" max="5.0" step="0.1"
                    placeholder="0-5">
            </div>
            <!-- WiFi Mode Configuration -->
            <div class="sensor-config">
                <h2>WiFi Settings</h2>
                <label for="wifiMode">WiFi Mode</label>
                <select id="wifiMode" name="wifiMode" onchange="updateApPasswordState()">
                    <option value="0" {wifiModeAP}>Access Point</option>
                    <option value="1" {wifiModeSTA}>Station</option>
                </select><br>
                <label for="apPassword">AP Password</label>
                <input type="text" id="apPassword" name="apPassword" value="{apPassword}" placeholder="AP Password">
            </div>
            <input type="submit" value="Save">
        </form>
    </div>
</body>

</html>
)rawliteral";