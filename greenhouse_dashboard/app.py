from flask import Flask, jsonify, request, render_template
import requests
from flask_sqlalchemy import SQLAlchemy
from datetime import datetime, timezone, timedelta
from zoneinfo import ZoneInfo

app = Flask(__name__, template_folder='templates', static_folder='static')
# Timezone for Rajkot, India (fallback for Windows without tzdata)
try:
    IST = ZoneInfo("Asia/Kolkata")
except Exception:
    IST = timezone(timedelta(hours=5, minutes=30))

# Database configuration
app.config['SQLALCHEMY_DATABASE_URI'] = 'sqlite:///greenhouse.db'
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False
db = SQLAlchemy(app)

# --- Your Blynk Auth Token ---
BLYNK_TOKEN = "nXZ1OWqfJpubOU9RmiajKTpk3iwnzZyF"
# Use global host (auto routes to region)
BLYNK_BASE = "https://blynk.cloud"

# --- Device States ---
state = {
    "pump": False,
    "fan": False
}

# --- Local sensor cache (from ESP32 > /api/logs) ---
sensor_cache = {
    "temperature": None,
    "humidity": None,
    "soil": None,
    "timestamp": None,
}

# --- Database Models ---
def now_ist():
    return datetime.now(IST).replace(tzinfo=None)

class Reading(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    timestamp = db.Column(db.DateTime, default=now_ist, index=True)
    temperature = db.Column(db.Float, nullable=True)
    humidity = db.Column(db.Float, nullable=True)
    soil = db.Column(db.Integer, nullable=True)
    pump = db.Column(db.Boolean, default=False)
    fan = db.Column(db.Boolean, default=False)
    source = db.Column(db.String(20), default='unknown')


# 🔹 Fetch Live Data from Blynk Cloud

def get_blynk_data():
    """Fetch temperature (V3), humidity (V4), soil moisture (V5) from Blynk."""
    try:
        pins = ["V3", "V4", "V5", "V6", "V7"]
        url = (
            f"{BLYNK_BASE}/external/api/batch/get?token="
            f"{BLYNK_TOKEN}&" + "&".join(pins)
        )
        response = requests.get(url, timeout=8)
        if response.status_code == 200:
            data = response.json()
        else:
            print(
                "[WARN] batch/get failed, falling back to /get per pin",
                {"status": response.status_code, "body": response.text[:200]}
            )
            # Fallback: fetch each pin separately
            data = {}
            for p in pins:
                try:
                    single_url = f"{BLYNK_BASE}/external/api/get?token={BLYNK_TOKEN}&{p}"
                    r = requests.get(single_url, timeout=5)
                    if r.status_code == 200:
                        data[p] = r.text.strip().strip('"')
                    else:
                        print("[ERROR] get", p, r.status_code, r.text[:120])
                        data[p] = 0
                except Exception as ie:
                    print("[ERROR] exception fetching", p, ie)
                    data[p] = 0

        try:
            temperature = float(data.get("V3", 0) or 0)
        except Exception:
            temperature = 0.0
        try:
            humidity = float(data.get("V4", 0) or 0)
        except Exception:
            humidity = 0.0
        try:
            soil = int(float(data.get("V5", 0) or 0))
        except Exception:
            soil = 0
        try:
            pump_state = bool(int(float(data.get("V6", 0) or 0)))
        except Exception:
            pump_state = False
        try:
            fan_state = bool(int(float(data.get("V7", 0) or 0)))
        except Exception:
            fan_state = False

        state["pump"] = pump_state
        state["fan"] = fan_state

        soil_status = (
            '🌵 Very Dry' if soil <= 20 else
            '💧 Dry' if soil <= 40 else
            '🌿 Moist' if soil <= 70 else
            '🌧 Wet'
        )

        return {
            "temperature": round(temperature, 1),
            "humidity": round(humidity, 1),
            "soil": soil,
            "soilStatus": soil_status,
            "pump": pump_state,
            "fan": fan_state
        }

    except Exception as e:
        print(f"[ERROR] Failed to fetch from Blynk: {e}")
        return None



# 🔹 Flask Routes

@app.route('/')
def index():
    return render_template('simple_dashboard.html')

@app.route('/simple')
def simple():
    return render_template('simple_dashboard.html')

@app.route('/api/data')
def api_data():
    """Get real-time data from Blynk Cloud."""
    data = get_blynk_data()
    if data:
        print("[API DATA]", data)
        try:
            reading = Reading(
                temperature=data.get("temperature"),
                humidity=data.get("humidity"),
                soil=data.get("soil"),
                pump=bool(data.get("pump", False)),
                fan=bool(data.get("fan", False)),
                source='blynk'
            )
            db.session.add(reading)
            db.session.commit()
        except Exception as e:
            print("[WARN] Failed to save reading from Blynk:", e)
        return jsonify(data)
    # Fallback to local sensor cache if available
    if sensor_cache["temperature"] is not None and sensor_cache["humidity"] is not None and sensor_cache["soil"] is not None:
        soil = int(sensor_cache["soil"] or 0)
        soil_status = (
            '🌵 Very Dry' if soil <= 20 else
            '💧 Dry' if soil <= 40 else
            '🌿 Moist' if soil <= 70 else
            '🌧 Wet'
        )
        fallback = {
            "temperature": round(float(sensor_cache["temperature"] or 0), 1),
            "humidity": round(float(sensor_cache["humidity"] or 0), 1),
            "soil": soil,
            "soilStatus": soil_status,
            "pump": state["pump"],
            "fan": state["fan"],
            "source": "local-cache",
            "timestamp": sensor_cache["timestamp"],
        }
        print("[API DATA - FALLBACK]", fallback)
        return jsonify(fallback)
    return jsonify({"error": "Failed to get data from Blynk"}), 500


@app.route('/api/toggle')
def api_toggle():
    """Toggle pump or fan via Blynk Cloud."""
    device = request.args.get('device', '').lower()
    if device not in ('pump', 'fan'):
        return 'Invalid device', 400

    pin = 'V6' if device == 'pump' else 'V7'
    new_state = not state[device]

    try:
        # Update on Blynk Cloud
        update_url = (
            f"{BLYNK_BASE}/external/api/update?token="
            f"{BLYNK_TOKEN}&{pin}={int(new_state)}"
        )
        r = requests.get(update_url, timeout=5)
        if r.status_code != 200:
            print(
                "[ERROR] Blynk update failed",
                {
                    "status": r.status_code,
                    "url": f"{BLYNK_BASE}/external/api/update?...",
                    "body": r.text[:500],
                },
            )
            r.raise_for_status()
        state[device] = new_state
        print(f"[INFO] {device.capitalize()} toggled to {new_state}")
        return jsonify({device: new_state})
    except Exception as e:
        print(f"[ERROR] Failed to toggle {device}: {e}")
        return jsonify({"error": str(e)}), 500


@app.route('/api/logs', methods=['POST'])
def api_logs():
    """Receive sensor data from ESP32 for local display/logging."""
    data = request.get_json()
    if not data:
        return jsonify({"status": "error", "message": "No data received"}), 400

    print(f"[ESP32 LOG] {data}")
    try:
        # Map incoming keys to our cache
        t = data.get("temperature")
        h = data.get("humidity")
        s = data.get("moisture") if "moisture" in data else data.get("soil")
        ts = data.get("timestamp")
        if t is not None:
            sensor_cache["temperature"] = float(t)
        if h is not None:
            sensor_cache["humidity"] = float(h)
        if s is not None:
            sensor_cache["soil"] = int(float(s))
        if ts:
            sensor_cache["timestamp"] = ts
    except Exception as e:
        print("[WARN] Failed to update sensor cache:", e)
    try:
        reading = Reading(
            temperature=sensor_cache.get("temperature"),
            humidity=sensor_cache.get("humidity"),
            soil=sensor_cache.get("soil"),
            pump=state.get("pump", False),
            fan=state.get("fan", False),
            source='esp32'
        )
        db.session.add(reading)
        db.session.commit()
    except Exception as e:
        print("[WARN] Failed to save reading from ESP32:", e)
    return jsonify({"status": "success"}), 200


@app.route('/api/readings')
def api_readings():
    """Fetch recent readings from the database."""
    try:
        limit = int(request.args.get('limit', 100))
    except Exception:
        limit = 100
    rows = Reading.query.order_by(Reading.timestamp.desc()).limit(limit).all()
    payload = [
        {
            "id": r.id,
            "timestamp": r.timestamp.isoformat(),
            "temperature": r.temperature,
            "humidity": r.humidity,
            "soil": r.soil,
            "pump": r.pump,
            "fan": r.fan,
            "source": r.source,
        }
        for r in rows
    ]
    return jsonify(payload)



# 🔹 Main

if __name__ == '__main__':
    print("🌿 Flask Server Running: http://172.20.10.2:5000")
    # Ensure tables exist
    with app.app_context():
        db.create_all()
    app.run(host='0.0.0.0', port=5000, debug=True)
