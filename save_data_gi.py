import socket
import sqlite3
import json
from tabulate import tabulate
from datetime import datetime

# === SQLite Setup ===
db_path = "/Users/marionridgway/Documents/reactor_project/reactor_data.db"
conn = sqlite3.connect(db_path)
cursor = conn.cursor()

# Create 3 tables: experiments, reagents, sensor_log
cursor.executescript('''
CREATE TABLE IF NOT EXISTS experiments (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    exp_number TEXT UNIQUE,
    operator TEXT,
    description TEXT,
    start_time TEXT,
    end_time TEXT
);

CREATE TABLE IF NOT EXISTS reagents (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    exp_number TEXT,
    reagent TEXT,
    concentration REAL
);

CREATE TABLE IF NOT EXISTS sensor_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT DEFAULT CURRENT_TIMESTAMP,
    exp_number TEXT,
    temperature REAL,
    uv1 REAL,
    photodiode REAL,
    turbidity1 REAL,
    turbidity2 REAL,
    rgb1_r INTEGER,
    rgb1_g INTEGER,
    rgb1_b INTEGER,
    rgb2_r INTEGER,
    rgb2_g INTEGER,
    rgb2_b INTEGER,
    uv_led_state INTEGER,
    uv_intensity INTEGER,
    pump1_state INTEGER,
    pump2_state INTEGER,
    pump_speed REAL,
    flow_rate REAL
);
''')
conn.commit()

# === TCP Setup ===
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind(('localhost', 9000))
sock.listen(1)

print("🔌 Waiting for Node-RED connection on port 9000...")
conn_sock, addr = sock.accept()
print(f"✅ Connected by {addr}")

# Track current experiment
current_exp_number = None

# def print_latest_rows():
    #"""Prints the last 5 rows of sensor_log (all columns)."""
    # ursor.execute("SELECT * FROM sensor_log ORDER BY timestamp DESC LIMIT 5")
    #rows = cursor.fetchall()
    #col_names = [description[0] for description in cursor.description]

    #formatted_rows = []
    #for row in rows:
        #formatted_rows.append([
            #round(val, 3) if isinstance(val, float) else val for val in row
        #])

    #print("\n📊 Last 5 Sensor Log Rows:")
    #print(tabulate(formatted_rows, headers=col_names, tablefmt="plain"))

def print_experiment_metadata(exp_number):
    """Print metadata and reagents for a given experiment."""
    cursor.execute("SELECT * FROM experiments WHERE exp_number = ?", (exp_number,))
    exp_row = cursor.fetchone()
    if exp_row:
        exp_cols = [description[0] for description in cursor.description]
        print("\n🧪 Experiment Metadata:")
        for col, val in zip(exp_cols, exp_row):
            print(f"  {col}: {val}")

        cursor.execute("SELECT reagent, concentration FROM reagents WHERE exp_number = ?", (exp_number,))
        reagents = cursor.fetchall()
        if reagents:
            print("  Reagents:")
            for r in reagents:
                print(f"    - {r[0]} ({r[1]} M)")
        else:
            print("  Reagents: None")
    else:
        print(f"\nℹ️ No experiment metadata found for '{exp_number}'.")

# === Main Loop ===
while True:
    data = conn_sock.recv(4096)
    if not data:
        break

    try:
        line = data.decode().strip()
        print("RAW MESSAGE RECEIVED:", line)
        if not line.startswith("{"):
            continue

        parsed = json.loads(line)

        # --- Handle experiment start ---
        if parsed.get("type") == "setup":
            exp = parsed["experiment"]
            current_exp_number = exp["expNo"]

            # Always insert a new row (no replacing)
            cursor.execute('''
                INSERT INTO experiments (exp_number, operator, description, start_time)
                VALUES (?, ?, ?, datetime('now'))
            ''', (current_exp_number, exp["operator"], exp["description"]))
            conn.commit()

            # Insert reagents
            for r in exp.get("reagents", []):
                cursor.execute('''
                    INSERT INTO reagents (exp_number, reagent, concentration)
                    VALUES (?, ?, ?)
                ''', (current_exp_number, r["name"], r.get("concentration", None)))
            conn.commit()

            print(f"🆕 Experiment '{current_exp_number}' started and saved.")
            print_experiment_metadata(current_exp_number)
            continue

        # --- Handle experiment stop ---
        if parsed.get("type") == "stop":
            if current_exp_number:
                cursor.execute(
                    "UPDATE experiments SET end_time = datetime('now') WHERE exp_number = ?",
                    (current_exp_number,)
                )
                conn.commit()
                print(f"🛑 Experiment '{current_exp_number}' stopped.")
                print_experiment_metadata(current_exp_number)
                current_exp_number = None
            continue

        # --- Handle sensor data ---
        if not current_exp_number:
            print("⚠️ No active experiment. Ignoring sensor data.")
            continue

        cursor.execute('''
            INSERT INTO sensor_log (
                exp_number, temperature, uv1, photodiode,
                turbidity1, turbidity2,
                rgb1_r, rgb1_g, rgb1_b,
                rgb2_r, rgb2_g, rgb2_b,
                uv_led_state, uv_intensity,
                pump1_state, pump2_state,
                pump_speed, flow_rate
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ''', (
            current_exp_number,
            parsed.get("temp"), parsed.get("uv1"), parsed.get("photodiode"),
            parsed.get("turbidity"), parsed.get("turbidity2"),
            parsed.get("rgb1_r"), parsed.get("rgb1_g"), parsed.get("rgb1_b"),
            parsed.get("rgb2_r"), parsed.get("rgb2_g"), parsed.get("rgb2_b"),
            parsed.get("uvLed"), parsed.get("uvIntensity"),
            parsed.get("pump"), parsed.get("pump2"),
            parsed.get("pumpSpeed"), parsed.get("flowRate")
        ))
        conn.commit()

        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        print(f"\n✅ Logged sensor data at {now}")


    except Exception as e:
        print("⚠️ Error:", e)
        continue

conn_sock.close()
conn.close()
