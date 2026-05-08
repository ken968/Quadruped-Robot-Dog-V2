from fastapi import FastAPI
from fastapi.responses import HTMLResponse
import uvicorn
import json

# Mengambil alat dari mqtt_client.py
from mqtt_client import client, connect_mqtt, COMMAND, TOPIC_CMD

app = FastAPI()

# Jalankan MQTT saat server web dinyalakan
connect_mqtt()

# Desain Web UI (HTML + CSS) - Ini yang akan muncul di layar HP Anda
HTML_PAGE = """
<!DOCTYPE html>
<html>
<head>
    <title>RoboDog Dashboard</title>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; text-align: center; background-color: #121212; color: white; margin-top: 20px; touch-action: manipulation; }
        .btn { padding: 15px 25px; font-size: 20px; margin: 8px; border-radius: 12px; border: none; cursor: pointer; color: white; box-shadow: 0 4px 6px rgba(0,0,0,0.3); font-weight: bold; }
        .btn-blue { background-color: #3b82f6; }
        .btn-red { background-color: #ef4444; }
        .btn-green { background-color: #10b981; }
        .btn-gray { background-color: #4b5563; }
        .btn:active { transform: scale(0.95); opacity: 0.8; box-shadow: none; }
        
        /* Grid untuk merakit Joystick (D-PAD) */
        .dpad { display: grid; grid-template-columns: 80px 80px 80px; justify-content: center; gap: 10px; margin: 40px 0; }
        .empty { visibility: hidden; }
    </style>
</head>
<body>
    <h2>RoboDog Command Center</h2>
    
    <!-- Tombol Joystick WASD -->
    <div class="dpad">
        <div class="empty"></div>
        <button class="btn btn-blue" onmousedown="sendCmd('MAJU')" onmouseup="sendCmd('BERHENTI')" ontouchstart="sendCmd('MAJU')" ontouchend="sendCmd('BERHENTI')">W</button>
        <div class="empty"></div>
        
        <button class="btn btn-blue" onmousedown="sendCmd('KIRI')" onmouseup="sendCmd('BERHENTI')" ontouchstart="sendCmd('KIRI')" ontouchend="sendCmd('BERHENTI')">A</button>
        <button class="btn btn-red" onclick="sendCmd('BERHENTI')"></button>
        <button class="btn btn-blue" onmousedown="sendCmd('KANAN')" onmouseup="sendCmd('BERHENTI')" ontouchstart="sendCmd('KANAN')" ontouchend="sendCmd('BERHENTI')">D</button>
        
        <div class="empty"></div>
        <button class="btn btn-blue" onmousedown="sendCmd('MUNDUR')" onmouseup="sendCmd('BERHENTI')" ontouchstart="sendCmd('MUNDUR')" ontouchend="sendCmd('BERHENTI')">S</button>
        <div class="empty"></div>
    </div>

    <!-- Tombol Fitur -->
    <div>
        <button class="btn btn-gray" onclick="sendCmd('DUDUK')">Duduk (E)</button>
        <button class="btn btn-gray" onclick="sendCmd('TIDUR')">Tidur (Q)</button>
        <br><br>
        <button class="btn btn-green" onclick="sendCmd('AUTOPILOT')">AutoPilot</button>
        <button class="btn btn-blue" onclick="sendCmd('MANUAL')">Manual</button>
        <br><br>
        <button class="btn btn-green" onclick="sendCmd('LEDON')">LED ON</button>
        <button class="btn btn-red" onclick="sendCmd('LEDOFF')">LED OFF</button>
    </div>

    <script>
        // Fungsi untuk menembak data dari HP ke Python
        function sendCmd(cmdName) {
            fetch("/api/command/" + cmdName, {method: "POST"});
        }
    </script>
</body>
</html>
"""

# Jika buka IP address di browser, tampilkan halaman Web
@app.get("/", response_class=HTMLResponse)
async def home():
    return HTML_PAGE

# Jika tombol ditekan di Web, kirim perintah ke MQTT
@app.post("/api/command/{cmd_name}")
async def api_send_command(cmd_name: str):
    if cmd_name in COMMAND:
        print(f"Aksi dari WEB: {cmd_name}\n")
        client.publish(TOPIC_CMD, json.dumps(COMMAND[cmd_name]) + '\n')
        return {"status": "sukses", "command": cmd_name}
    return {"status": "gagal", "pesan": "Perintah tidak ada"}

if __name__ == "__main__":
    print("Web Server Menyala! Buka http://localhost:8081 di browser Anda.")
    # host="0.0.0.0" artinya web ini bisa diakses dari HP yang satu WiFi dengan laptop
    uvicorn.run(app, host="0.0.0.0", port=8081)
