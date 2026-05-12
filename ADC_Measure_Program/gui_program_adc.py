#!/usr/bin/env python3
import socket
import struct
import zlib
import sqlite3
import time
import tkinter as tk
from tkinter import ttk, messagebox

# Settings
UUT_IP = "192.168.10.2"
UUT_PORT = 5005
DB_FILE = "verification_results.db"

class VerificationApp:
    def __init__(self, root):
        self.root = root
        self.root.title("UUT Verification System")
        self.root.geometry("600x550")
        self.root.configure(bg="#1e1e2e")  # Dark background
        
        self.live_mode = False
        self.init_database()
        self.setup_styles()
        self.setup_ui()

    def init_database(self):
        with sqlite3.connect(DB_FILE) as conn:
            conn.execute('''CREATE TABLE IF NOT EXISTS adc_logs 
                           (id INTEGER PRIMARY KEY AUTOINCREMENT, 
                            timestamp DATETIME, 
                            voltage REAL, 
                            status TEXT)''')

    def setup_styles(self):
        style = ttk.Style()
        style.theme_use('clam')
        
        # Configure modern colors
        style.configure("Treeview", background="#2b2b3b", foreground="white", fieldbackground="#2b2b3b", rowheight=25)
        style.map("Treeview", background=[('selected', '#454561')])
        style.configure("TButton", font=("Arial", 10, "bold"))
        style.configure("Header.TLabel", background="#1e1e2e", foreground="#cdd6f4", font=("Arial", 14, "bold"))

    def setup_ui(self):
        # Header Section
        header_frame = tk.Frame(self.root, bg="#1e1e2e")
        header_frame.pack(fill="x", pady=20)
        ttk.Label(header_frame, text="ADC REAL-TIME MONITOR (3.3V RAIL)", style="Header.TLabel").pack()

        # Big Voltage Display Card
        card = tk.Frame(self.root, bg="#2b2b3b", bd=2, relief="flat", padx=20, pady=20)
        card.pack(padx=30, fill="x")

        self.volt_var = tk.StringVar(value="0.00 V")
        self.volt_label = tk.Label(card, textvariable=self.volt_var, bg="#2b2b3b", 
                                  foreground="#a6e3a1", font=("Arial", 48, "bold"))
        self.volt_label.pack()

        self.status_var = tk.StringVar(value="Status: Ready")
        tk.Label(card, textvariable=self.status_var, bg="#2b2b3b", 
                 foreground="#89dceb", font=("Arial", 10)).pack()

        # Control Buttons
        btn_frame = tk.Frame(self.root, bg="#1e1e2e")
        btn_frame.pack(pady=20)

        self.sync_btn = ttk.Button(btn_frame, text="Single Sync", command=self.request_adc, width=15)
        self.sync_btn.pack(side="left", padx=5)

        self.live_btn = ttk.Button(btn_frame, text="Start Live Mode", command=self.toggle_live_mode, width=15)
        self.live_btn.pack(side="left", padx=5)

        # History Table
        table_frame = tk.Frame(self.root, bg="#1e1e2e")
        table_frame.pack(fill="both", expand=True, padx=30, pady=10)

        self.tree = ttk.Treeview(table_frame, columns=("Time", "Voltage", "Status"), show="headings")
        self.tree.heading("Time", text="Timestamp")
        self.tree.heading("Voltage", text="Voltage (V)")
        self.tree.heading("Status", text="Result")
        self.tree.column("Time", width=150)
        self.tree.column("Voltage", width=100)
        self.tree.pack(fill="both", expand=True, side="left")

        scrollbar = ttk.Scrollbar(table_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")

        self.load_history()

    def calculate_crc32(self, data):
        return zlib.crc32(data) & 0xFFFFFFFF

    def toggle_live_mode(self):
        self.live_mode = not self.live_mode
        if self.live_mode:
            self.live_btn.configure(text="Stop Live Mode")
            self.run_live_loop()
        else:
            self.live_btn.configure(text="Start Live Mode")

    def run_live_loop(self):
        if self.live_mode:
            self.request_adc()
            self.root.after(500, self.run_live_loop) # Update every 500ms

    def request_adc(self):
        try:
            # 1. Prepare Packet (Opcode=16, Length=0)
            header = struct.pack("=B H", 16, 0)
            packet = header + struct.pack("<I", self.calculate_crc32(header))

            # 2. Network exchange
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(1.0)
                sock.sendto(packet, (UUT_IP, UUT_PORT))
                raw_data, _ = sock.recvfrom(1024)

            # 3. Validate Response
            payload = raw_data[:-4]
            received_crc = struct.unpack("<I", raw_data[-4:])[0]
            
            if self.calculate_crc32(payload) != received_crc:
                raise ValueError("CRC Error")

            # 4. Unpack
            _, _, adc_raw = struct.unpack("=B H H", payload)
            voltage = (adc_raw / 4095.0) * 3.3
            
            # 5. Update UI
            self.volt_var.set(f"{voltage:.3f} V")
            self.status_var.set(f"Last sync: {time.strftime('%H:%M:%S')}")
            
            # Color coding: Red if voltage is out of expected range (e.g., < 3.0V)
            if voltage < 3.1: self.volt_label.configure(foreground="#f38ba8")
            else: self.volt_label.configure(foreground="#a6e3a1")

            self.save_to_db(voltage)

        except Exception as e:
            self.status_var.set(f"Error: {str(e)}")
            if not self.live_mode:
                messagebox.showerror("Network Error", "Could not reach UUT.")

    def save_to_db(self, voltage):
        ts = time.strftime('%Y-%m-%d %H:%M:%S')
        status = "PASS" if voltage > 3.1 else "FAIL"
        with sqlite3.connect(DB_FILE) as conn:
            conn.execute("INSERT INTO adc_logs (timestamp, voltage, status) VALUES (?,?,?)", 
                        (ts, voltage, status))
        
        # Insert into top of table
        self.tree.insert("", 0, values=(ts, f"{voltage:.3f}", status))
        if len(self.tree.get_children()) > 50: # Keep UI snappy
            self.tree.delete(self.tree.get_children()[-1])

    def load_history(self):
        with sqlite3.connect(DB_FILE) as conn:
            cursor = conn.execute("SELECT timestamp, voltage, status FROM adc_logs ORDER BY id DESC LIMIT 20")
            for row in cursor:
                self.tree.insert("", "end", values=row)

if __name__ == "__main__":
    root = tk.Tk()
    app = VerificationApp(root)
    root.mainloop()