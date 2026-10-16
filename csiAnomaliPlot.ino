import serial
import sys
import numpy as np
import matplotlib.pyplot as plt
import time
import csv
from collections import deque

# --- CONFIGURATION ---
SERIAL_PORT = 'COM5'  
BAUD_RATE = 921600
HISTORY_LEN = 150         # Number of recent packets to keep on screen
SENSITIVITY_THRESHOLD = 3.5 # Trigger threshold (Lower = more sensitive, Higher = less sensitive)

# CSV Logging Setup
csv_filename = "csi_detected_events.csv"
with open(csv_filename, "w", newline='') as f:
    writer = csv.writer(f)
    writer.writerow(["timestamp", "elapsed_sec", "packet_id", "avg_amplitude", "motion_energy", "is_anomaly"])

# Rolling buffers for real-time time-series plotting
time_history = deque(maxlen=HISTORY_LEN)
amp_mean_history = deque(maxlen=HISTORY_LEN)
motion_energy_history = deque(maxlen=HISTORY_LEN)
anomaly_history = deque(maxlen=HISTORY_LEN)

# Setup Matplotlib GUI
plt.ion() 
fig, (ax_amp, ax_motion) = plt.subplots(2, 1, figsize=(11, 8))
fig.canvas.manager.set_window_title('Real-Time ESP32 CSI Anomaly & Motion Detector')

# Top Plot: Filtered Mean Amplitude over Time
line_amp, = ax_amp.plot([], [], 'b-', linewidth=2, label="Mean CSI Amplitude")
ax_amp.set_title("1. Filtered Signal Strength (Mean Amplitude Over Time)", fontsize=12, fontweight='bold')
ax_amp.set_ylabel("Amplitude")
ax_amp.grid(True, linestyle='--', alpha=0.5)

# Bottom Plot: Motion Energy & Anomaly Detection
line_motion, = ax_motion.plot([], [], 'g-', linewidth=2, label="Motion Energy (Δ Amplitude)")
line_thresh = ax_motion.axhline(y=SENSITIVITY_THRESHOLD, color='r', linestyle='--', linewidth=2, label="Anomaly Threshold")
ax_motion.set_title("2. Motion Energy & Sudden Variance Detector", fontsize=12, fontweight='bold')
ax_motion.set_ylabel("Variation (Δ Rate)")
ax_motion.set_xlabel("Elapsed Time (Seconds)")
ax_motion.grid(True, linestyle='--', alpha=0.5)
ax_motion.legend(loc="upper left")

# Status Text Banner on Plot
status_text = fig.text(0.5, 0.95, "STATUS: CALIBRATING / IDLE", 
                       ha='center', va='center', fontsize=14, fontweight='bold', 
                       bbox=dict(boxstyle='round,pad=0.5', facecolor='green', alpha=0.5, edgecolor='black'))

plt.tight_layout(rect=[0, 0.03, 1, 0.93])
plt.show() 

# Open Serial Port
ser = None 
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.01)
    print(f"Successfully opened {SERIAL_PORT}! Listening for CSI...")
except Exception as e:
    print(f"\n[!] Error opening {SERIAL_PORT}: {e}")
    sys.exit(1)

prev_amplitude = None
start_time = time.time()
packet_id = 0

try:
    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        
        if line.startswith("CSI_DATA"):
            parts = line.split(',')
            
            # 1 header + 384 integers = 385 parts (40MHz / 192 subcarriers)
            if len(parts) == 385:
                try:
                    data = np.array(parts[1:], dtype=int)
                    I = data[0::2]
                    Q = data[1::2]
                    
                    # 1. Raw Subcarrier Calculations
                    current_amplitude = np.sqrt(I**2 + Q**2)
                    mean_amp = np.mean(current_amplitude)
                    
                    # 2. Motion Energy Calculation (First-Order Difference across subcarriers)
                    if prev_amplitude is not None:
                        # Average change across all 192 subcarriers since last packet
                        motion_energy = np.mean(np.abs(current_amplitude - prev_amplitude))
                    else:
                        motion_energy = 0.0
                        
                    prev_amplitude = current_amplitude.copy()
                    
                    # 3. Anomaly Detection Logic
                    is_anomaly = motion_energy > SENSITIVITY_THRESHOLD
                    
                    current_time = time.time()
                    elapsed_sec = round(current_time - start_time, 2)
                    
                    # Append data to rolling window buffers
                    time_history.append(elapsed_sec)
                    amp_mean_history.append(mean_amp)
                    motion_energy_history.append(motion_energy)
                    anomaly_history.append(is_anomaly)
                    
                    # Log event to CSV
                    with open(csv_filename, "a", newline='') as f:
                        writer = csv.writer(f)
                        writer.writerow([current_time, elapsed_sec, packet_id, round(mean_amp, 2), round(motion_energy, 2), int(is_anomaly)])
                    
                    # 4. Real-time Plot Updating
                    line_amp.set_data(time_history, amp_mean_history)
                    line_motion.set_data(time_history, motion_energy_history)
                    
                    # Adjust X axes dynamically to follow rolling time window
                    ax_amp.set_xlim(min(time_history), max(time_history) + 0.5)
                    ax_amp.set_ylim(min(amp_mean_history) - 5, max(amp_mean_history) + 5)
                    
                    ax_motion.set_xlim(min(time_history), max(time_history) + 0.5)
                    ax_motion.set_ylim(0, max(max(motion_energy_history) + 2, SENSITIVITY_THRESHOLD + 2))
                    
                    # 5. Visual Alert Triggering
                    if is_anomaly:
                        status_text.set_text("🚨 MOTION / ANOMALY DETECTED!")
                        status_text.set_bbox(dict(boxstyle='round,pad=0.5', facecolor='red', alpha=0.8, edgecolor='black'))
                        print(f"[{elapsed_sec:06.2f}s] Packet {packet_id:04d} ---> 🚨 ANOMALY! (Energy: {motion_energy:.2f})")
                    else:
                        status_text.set_text("✅ STATUS: CLEAR / NO MOTION")
                        status_text.set_bbox(dict(boxstyle='round,pad=0.5', facecolor='lightgreen', alpha=0.8, edgecolor='black'))
                        if packet_id % 10 == 0:
                            print(f"[{elapsed_sec:06.2f}s] Packet {packet_id:04d} | Status: Clear | Motion Energy: {motion_energy:.2f}")
                    
                    plt.pause(0.001)
                    packet_id += 1
                    
                except ValueError:
                    pass
        else:
            plt.pause(0.001)

except KeyboardInterrupt:
    print("\nDetection stopped by user.")
finally:
    if ser is not None and ser.is_open:
        ser.close()
        print("Serial port closed safely.")