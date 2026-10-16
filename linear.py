import serial
import threading
import queue
import time
import cv2
import numpy as np
import csv
import sys
import os

# --- 1. DYNAMIC CONFIGURATION ---
# Start with 2 nodes. To scale to 3 or 4, just add them here!
ACTIVE_NODES = {
    1: 'COM5',  # Replace with your actual COM ports
    2: 'COM6'   
    # 3: 'COM7',
    # 4: 'COM8'
}
BAUD_RATE = 921600
SYNC_TOLERANCE_SEC = 0.015  # 15 milliseconds window to group packets
DATASET_NAME = "dataset_run_1"

# Buffers and Locks
data_queue = queue.Queue()
is_recording = True

# --- 2. SERIAL READER THREAD ---
def serial_worker(node_id, port):
    try:
        ser = serial.Serial(port, BAUD_RATE, timeout=0.5)
        print(f"[Node {node_id}] Connected on {port}")
    except Exception as e:
        print(f"[Node {node_id}] FAILED to open {port}: {e}")
        return

    while is_recording:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line.startswith("CSI_DATA"):
                parts = line.split(',')
                # "CSI_DATA" + Node_ID + 384 values = 386 parts
                if len(parts) == 386:
                    recv_node = int(parts[1])
                    if recv_node == node_id:
                        payload = np.array(parts[2:], dtype=np.int8)
                        # Push to Sync Engine: (Timestamp, NodeID, Payload)
                        data_queue.put((time.time(), node_id, payload))
        except Exception:
            pass
    ser.close()

# --- 3. WEBCAM THREAD ---
def webcam_worker():
    cap = cv2.VideoCapture(0) # 0 is default laptop webcam
    if not cap.isOpened():
        print("[Webcam] FAILED to open camera.")
        return
        
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = 30.0
    
    # Save video as MP4
    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    out = cv2.VideoWriter(f"{DATASET_NAME}_video.mp4", fourcc, fps, (width, height))
    
    # Log exact frame timestamps to sync with CSI later
    frame_csv = open(f"{DATASET_NAME}_frames.csv", 'w', newline='')
    frame_writer = csv.writer(frame_csv)
    frame_writer.writerow(["frame_id", "timestamp"])
    
    frame_count = 0
    print("[Webcam] Recording started...")
    
    while is_recording:
        ret, frame = cap.read()
        if ret:
            ts = time.time()
            out.write(frame)
            frame_writer.writerow([frame_count, ts])
            
            # Show live feed
            cv2.imshow("Ground Truth Recording (Press 'q' in terminal to stop)", frame)
            cv2.waitKey(1)
            frame_count += 1
            
    cap.release()
    out.release()
    frame_csv.close()
    cv2.destroyAllWindows()
    print("[Webcam] Recording saved.")

# --- 4. MASTER SYNC ENGINE ---
def sync_engine():
    required_nodes = set(ACTIVE_NODES.keys())
    current_matrix = {}
    window_start_time = 0
    
    csi_csv = open(f"{DATASET_NAME}_csi.csv", 'w', newline='')
    csi_writer = csv.writer(csi_csv)
    # Header: timestamp, node1_data, node2_data...
    header = ["timestamp"] + [f"node_{n}_IQ" for n in sorted(required_nodes)]
    csi_writer.writerow(header)

    synced_packets = 0

    while is_recording or not data_queue.empty():
        try:
            ts, node_id, payload = data_queue.get(timeout=1.0)
        except queue.Empty:
            continue

        # If this packet is outside the 15ms window, flush the window and start a new one
        if ts - window_start_time > SYNC_TOLERANCE_SEC:
            current_matrix = {}
            window_start_time = ts

        # Add packet to current time window
        current_matrix[node_id] = payload

        # Check if we have received a packet from ALL active nodes in this window
        if set(current_matrix.keys()) == required_nodes:
            # We have a perfect spatial match! Save it to CSV.
            row = [window_start_time]
            for n in sorted(required_nodes):
                # Convert numpy array to space-separated string for CSV storage
                row.append(" ".join(map(str, current_matrix[n])))
            
            csi_writer.writerow(row)
            synced_packets += 1
            
            # Print status every 100 successful matrices
            if synced_packets % 100 == 0:
                print(f"[Sync Engine] Captured {synced_packets} synchronized spatial matrices...")
                
            # Clear window for next packet blast
            current_matrix = {}

    csi_csv.close()
    print(f"[Sync Engine] Finished. Total synced matrices: {synced_packets}")

# --- MAIN EXECUTION ---
if __name__ == "__main__":
    print("--- 4-Channel Wi-Fi Spatial Radar ---")
    
    # 1. Start Serial Threads
    threads = []
    for node_id, port in ACTIVE_NODES.items():
        t = threading.Thread(target=serial_worker, args=(node_id, port))
        t.daemon = True
        t.start()
        threads.append(t)

    # 2. Start Sync Engine Thread
    sync_thread = threading.Thread(target=sync_engine)
    sync_thread.daemon = True
    sync_thread.start()

    # 3. Start Webcam Thread
    cam_thread = threading.Thread(target=webcam_worker)
    cam_thread.daemon = True
    cam_thread.start()

    # 4. Wait for user to stop
    try:
        input("\n>>> RECORDING LIVE. PRESS ENTER TO STOP COLLECTION <<<\n")
    except KeyboardInterrupt:
        pass
    finally:
        is_recording = False
        print("\nShutting down gracefully... saving files.")
        for t in threads:
            t.join()
        sync_thread.join()
        cam_thread.join()
        print("Done.")