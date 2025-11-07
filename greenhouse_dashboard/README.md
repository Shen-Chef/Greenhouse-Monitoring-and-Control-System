# Greenhouse Dashboard (Flask)

This project is a simple Flask backend that serves a web dashboard (dashboard.html) and provides a simulated sensor API.

## Requirements
- Python 3.8+

## Setup & Run (VS Code)

1. Open this folder in VS Code.
2. Create and activate a virtual environment (recommended):
   ```bash
   python -m venv venv
   # Windows
   venv\Scripts\activate
   # macOS/Linux
   source venv/bin/activate
   ```
3. Install requirements:
   ```bash
   pip install -r requirements.txt
   ```
4. Run the app:
   ```bash
   python app.py
   ```
5. Open your browser at `http://localhost:5000/`

## Notes
- The server simulates sensor data in a background thread. Connect your ESP32 later by replacing the simulation loop with real sensor updates (e.g., POST requests from device).
