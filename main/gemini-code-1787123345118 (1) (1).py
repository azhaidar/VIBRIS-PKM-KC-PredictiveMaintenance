# ==============================================================================
# VIBRIS INDUSTRIAL HMI - UI ARCHITECTURE REDESIGN (EXACT TARGET MATCH)
# Direktif: Peer-to-Peer, Brutal Technical Honesty, Zero Sycophancy.
# ==============================================================================

import sys
import os
import csv
import json
import time
from datetime import datetime
from collections import deque

try:
    import serial
    import serial.tools.list_ports
    import threading
except ImportError:
    serial = None

from PyQt5.QtWidgets import (
    QApplication, QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush

# ===================== KONFIGURASI OPERASIONAL =====================
SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 115200
LOG_DIR = "logs"
if not os.path.exists(LOG_DIR):
    os.makedirs(LOG_DIR)

ESP32_USB_HINTS = ["CH340", "CH343", "CP210", "USB-SERIAL", "USB SERIAL", "FTDI", "SILICON LABS", "ACM0"]

# ===================== PALET WARNA TEMA TARGET =====================
COL_BG_MAIN = "#101216"
COL_PANEL_DARK = "#181b20"
COL_ACCENT = "#38bdf8"
COL_ACCENT_DIM = "#1e2733"
COL_TEXT_LIGHT = "#e8eaed"
COL_TEXT_DIM = "#7c8592"
COL_OK = "#34d399"
COL_WARN = "#fbbf24"
COL_BAD = "#f87171"
COL_IDLE = "#94a3b8"
COL_HEADER_BG = "#15181e"

# ===================== KELAS UTAMA DASHBOARD =====================
class Dashboard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI | VIBRIS PIMNAS - EXACT UI TARGET")
        self.setWindowFlags(
            Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        )
        self.setFixedSize(480, 320)
        self.setStyleSheet(f"background-color: {COL_BG_MAIN}; color: {COL_TEXT_LIGHT}; font-family: Arial;")

        screen = QApplication.primaryScreen()
        if screen is not None:
            screen_geo = screen.geometry()
            self.move(screen_geo.x(), screen_geo.y())

        self.tick = 0
        self.last_raw_line = ""
        self.last_packet_time = time.time()
        self.packet_loss_flag = False

        self.current_v = 0.0
        self.current_a = 0.0
        self.current_temp = 0.0
        self.current_rpm = 0.0
        self.current_d2 = 0.0
        self.current_status_device = "normal"
        self.current_servis = "30+ hari"

        self.session_sample_count = 0
        self.session_waspada_count = 0
        self.session_bahaya_count = 0
        self.anomaly_events = []

        self.ser = None
        self.serial_connected = False

        self.machines = [
            {"name": "Mesin 1", "icon": "⚙️", "status_kalibrasi": True, "bearing_cmd": "A"},
            {"name": "Mesin 2", "icon": "⚙️", "status_kalibrasi": False, "bearing_cmd": "B"},
        ]
        self.selected_machine_idx = -1

        root = QVBoxLayout(self)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        # =========================================================================
        # HEADER IDENTIK DENGAN TARGET
        # =========================================================================
        header_frame = QFrame()
        header_frame.setStyleSheet(f"background-color: {COL_HEADER_BG}; border-radius: 4px; border: 1px solid #2a3542;")
        header_lay = QHBoxLayout(header_frame)
        header_lay.setContentsMargins(6, 4, 6, 4)

        self.btn_slot_selector = QPushButton("⚙️  - Pilih Slot -")
        self.btn_slot_selector.setStyleSheet(
            f"QPushButton {{ background-color: transparent; color: {COL_TEXT_LIGHT}; font-size: 8px; "
            f"font-weight: bold; border: none; text-align: left; }}"
            f"QPushButton:hover {{ color: {COL_ACCENT}; }}"
        )
        self.btn_slot_selector.clicked.connect(lambda: self._change_page(3))
        header_lay.addWidget(self.btn_slot_selector, 3)

        self.lbl_mode_awam = QLabel("MODE: AWAM")
        self.lbl_mode_awam.setStyleSheet("font-size: 7px; font-weight: bold; color: #38bdf8; background-color: #1e2733; padding: 2px 6px; border-radius: 3px;")
        header_lay.addWidget(self.lbl_mode_awam, 0, Qt.AlignCenter)

        header_right = QHBoxLayout()
        header_right.setSpacing(4)
        self.lbl_header_dot = QLabel("●")
        self.lbl_header_dot.setStyleSheet(f"font-size: 9px; color: {COL_WARN};")
        header_right.addWidget(self.lbl_header_dot)

        self.time_lbl = QLabel("15:23:15")
        self.time_lbl.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {COL_OK};")
        header_right.addWidget(self.time_lbl)

        header_lay.addLayout(header_right)
        root.addWidget(header_frame)

        # =========================================================================
        # STACKED WIDGET (4 MENU UTAMA SESUAI GAMBAR TARGET)
        # =========================================================================
        self.stack = QStackedWidget()
        self.stack.addWidget(self._page_beranda())    # Index 0
        self.stack.addWidget(self._page_riwayat())    # Index 1
        self.stack.addWidget(self._page_rekomendasi());# Index 2
        self.stack.addWidget(self._page_mesin_saya()) # Index 3
        root.addWidget(self.stack, 1)

        # =========================================================================
        # NAVIGASI BAWAH PERSIS TARGET
        # =========================================================================
        nav_bottom = QHBoxLayout()
        nav_bottom.setSpacing(4)
        
        self.btn_beranda = QPushButton("BERANDA")
        self.btn_riwayat = QPushButton("RIWAYAT")
        self.btn_rekomendasi = QPushButton("REKOMENDASI")
        self.btn_mesin_saya = QPushButton("MESIN SAYA")

        self.menu_buttons = [self.btn_beranda, self.btn_riwayat, self.btn_rekomendasi, self.btn_mesin_saya]

        for i, btn in enumerate(self.menu_buttons):
            btn.setFixedHeight(32)
            btn.clicked.connect(lambda checked, idx=i: self._change_page(idx))
            nav_bottom.addWidget(btn)
        
        root.addLayout(nav_bottom)

        self._init_serial_connection()

        self.main_timer = QTimer(self)
        self.main_timer.timeout.connect(self._update_gui)
        self.main_timer.start(200)

        self._change_page(0)
        self.show()

    def _update_nav_styles(self, active_idx):
        for i, btn in enumerate(self.menu_buttons):
            if i == active_idx:
                btn.setStyleSheet(f"background-color: {COL_ACCENT}; color: #101216; font-size: 8px; font-weight: bold; border-radius: 4px;")
            else:
                btn.setStyleSheet(f"background-color: #1e2733; color: {COL_TEXT_LIGHT}; font-size: 8px; font-weight: bold; border-radius: 4px; border: 1px solid #2a3542;")

    def _change_page(self, idx):
        self.stack.setCurrentIndex(idx)
        self._update_nav_styles(idx)

    # =========================================================================
    # PAGE 0: BERANDA (PERSIS SCREENSHOT 1)
    # =========================================================================
    def _page_beranda(self):
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(0, 2, 0, 2)
        lay.setSpacing(6)

        self.lbl_beranda_sub = QLabel("Mesin: - Pilih Slot -")
        self.lbl_beranda_sub.setAlignment(Qt.AlignCenter)
        self.lbl_beranda_sub.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {COL_TEXT_DIM};")
        lay.addWidget(self.lbl_beranda_sub)

        # Box Utama 1: Icon Status Besar
        box_status = QFrame()
        box_status.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid #2a3542; border-radius: 6px;")
        b_lay = QVBoxLayout(box_status)
        b_lay.setAlignment(Qt.AlignCenter)
        
        self.lbl_beranda_icon = QLabel("?")
        self.lbl_beranda_icon.setAlignment(Qt.AlignCenter)
        self.lbl_beranda_icon.setStyleSheet("font-size: 26px; color: #a0aec0; border: none;")
        b_lay.addWidget(self.lbl_beranda_icon)
        lay.addWidget(box_status, 2)

        # Box Utama 2: Status Check Text
        self.box_check_status = QFrame()
        self.box_check_status.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid #2a3542; border-radius: 6px;")
        c_lay = QVBoxLayout(self.box_check_status)
        c_lay.setAlignment(Qt.AlignCenter)
        
        self.lbl_check_text = QLabel("BELUM ADA HASIL CHECK")
        self.lbl_check_text.setAlignment(Qt.AlignCenter)
        self.lbl_check_text.setStyleSheet("font-size: 9px; font-weight: bold; color: #e2e8f0; border: none;")
        c_lay.addWidget(self.lbl_check_text)
        lay.addWidget(box_status, 2)
        lay.addWidget(self.box_check_status, 1)

        # Box Utama 3: Keterangan / Instruksi
        box_desc = QFrame()
        box_desc.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid #2a3542; border-radius: 6px;")
        d_lay = QVBoxLayout(box_desc)
        d_lay.setAlignment(Qt.AlignCenter)
        
        self.lbl_desc_text = QLabel("Pilih slot mesin, lalu jalankan sesi Check (1 menit) untuk melihat hasilnya di sini.")
        self.lbl_desc_text.setAlignment(Qt.AlignCenter)
        self.lbl_desc_text.setWordWrap(True)
        self.lbl_desc_text.setStyleSheet(f"font-size: 7px; color: {COL_TEXT_DIM}; border: none;")
        d_lay.addWidget(self.lbl_desc_text)
        lay.addWidget(box_desc, 1)

        # Box Footer: Estimasi Servis
        self.lbl_estimasi_servis = QLabel("Estimasi servis berikutnya: --")
        self.lbl_estimasi_servis.setAlignment(Qt.AlignCenter)
        self.lbl_estimasi_servis.setStyleSheet(f"font-size: 7px; font-weight: bold; color: {COL_TEXT_LIGHT}; background-color: {COL_PANEL_DARK}; padding: 4px; border-radius: 4px; border: 1px solid #2a3542;")
        lay.addWidget(self.lbl_estimasi_servis)

        return page

    # =========================================================================
    # PAGE 1: RIWAYAT (PERSIS SCREENSHOT 2)
    # =========================================================================
    def _page_riwayat(self):
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(0, 2, 0, 2)
        lay.setSpacing(6)

        lbl_title = QLabel("Riwayat Sesi Pemantauan Ini")
        lbl_title.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        lay.addWidget(lbl_title)

        # Grid 3 Kotak Statistik Atas
        grid_stat = QGridLayout()
        grid_stat.setSpacing(4)

        def _make_stat_box(title):
            f = QFrame()
            f.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid #2a3542; border-radius: 4px;")
            flay = QVBoxLayout(f)
            flay.setContentsMargins(2, 4, 2, 4)
            flay.setAlignment(Qt.AlignCenter)
            
            t = QLabel(title)
            t.setStyleSheet("font-size: 6px; color: #94a3b8; border: none;")
            t.setAlignment(Qt.AlignCenter)
            
            v = QLabel("0")
            v.setStyleSheet("font-size: 11px; font-weight: bold; color: #ffffff; border: none;")
            v.setAlignment(Qt.AlignCenter)
            
            flay.addWidget(t)
            flay.addWidget(v)
            return f, v

        box_w, self.lbl_stat_waspada = _make_stat_box("Kali Waspada")
        box_b, self.lbl_stat_bahaya = _make_stat_box("Kali Bahaya")
        box_t, self.lbl_stat_total = _make_stat_box("Total Data Masuk")

        grid_stat.addWidget(box_w, 0, 0)
        grid_stat.addWidget(box_b, 0, 1)
        grid_stat.addWidget(box_t, 0, 2)
        lay.addLayout(grid_stat)

        # Kotak Log Kejadian Terakhir
        lbl_kejadian = QLabel("Kejadian Terakhir:")
        lbl_kejadian.setStyleSheet(f"font-size: 7px; color: {COL_TEXT_DIM};")
        lay.addWidget(lbl_kejadian)

        self.list_riwayat = QListWidget()
        self.list_riwayat.setStyleSheet(f"background-color: {COL_PANEL_DARK}; color: #ffffff; border: 1px solid #2a3542; border-radius: 4px; font-size: 7px;")
        self.list_riwayat.addItem("Belum ada kejadian pada sesi ini.")
        lay.addWidget(self.list_riwayat, 1)

        lbl_hint_riwayat = QLabel("* Angka di atas otomatis mulai dari 0 tiap kali kamu pilih slot mesin untuk Check baru (durasi 1 menit).")
        lbl_hint_riwayat.setStyleSheet(f"font-size: 6px; color: {COL_TEXT_DIM}; font-style: italic;")
        lbl_hint_riwayat.setWordWrap(True)
        lay.addWidget(lbl_hint_riwayat)

        return page

    # =========================================================================
    # PAGE 2: REKOMENDASI (PERSIS SCREENSHOT 3)
    # =========================================================================
    def _page_rekomendasi(self):
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(0, 2, 0, 2)
        lay.setSpacing(6)

        lbl_title = QLabel("Rekomendasi Tindakan")
        lbl_title.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        lay.addWidget(lbl_title)

        box_rec = QFrame()
        box_rec.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid #2a3542; border-radius: 6px;")
        r_lay = QVBoxLayout(box_rec)
        r_lay.setContentsMargins(8, 8, 8, 8)
        r_lay.setSpacing(4)

        self.lbl_rec_penyebab = QLabel("Kemungkinan penyebab: belum ada hasil Check")
        self.lbl_rec_penyebab.setStyleSheet(f"font-size: 7px; font-weight: bold; color: {COL_WARN}; border: none;")
        self.lbl_rec_penyebab.setWordWrap(True)
        r_lay.addWidget(self.lbl_rec_penyebab)

        self.lbl_rec_tindakan = QLabel("Tindakan yang disarankan: pilih slot mesin lalu jalankan sesi Check (1 menit) dulu.")
        self.lbl_rec_tindakan.setStyleSheet(f"font-size: 7px; color: {COL_TEXT_LIGHT}; border: none;")
        self.lbl_rec_tindakan.setWordWrap(True)
        r_lay.addWidget(self.lbl_rec_tindakan)
        r_lay.addStretch(1)

        lay.addWidget(box_rec, 1)

        lbl_hint_rec = QLabel("* Rekomendasi otomatis berdasarkan pola sensor. Untuk kepastian, tetap periksa mesin secara langsung.")
        lbl_hint_rec.setStyleSheet(f"font-size: 6px; color: {COL_TEXT_DIM}; font-style: italic;")
        lbl_hint_rec.setWordWrap(True)
        lay.addWidget(lbl_hint_rec)

        return page

    # =========================================================================
    # PAGE 3: MESIN SAYA / PILIH SLOT (PERSIS SCREENSHOT 4)
    # =========================================================================
    def _page_mesin_saya(self):
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(0, 2, 0, 2)
        lay.setSpacing(6)

        lbl_title = QLabel("Pilih Slot Mesin")
        lbl_title.setAlignment(Qt.AlignCenter)
        lbl_title.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        lay.addWidget(lbl_title)

        lbl_desc = QLabel("Pilih salah satu, lalu di layar berikutnya kamu pilih sendiri mau Kalibrasi (rekam baseline baru) atau Check (bandingkan dengan baseline).")
        lbl_desc.setAlignment(Qt.AlignCenter)
        lbl_desc.setWordWrap(True)
        lbl_desc.setStyleSheet(f"font-size: 6px; color: {COL_TEXT_DIM};")
        lay.addWidget(lbl_desc)

        # Kartu Mesin 1 & Mesin 2
        cards_layout = QHBoxLayout()
        cards_layout.setSpacing(6)

        self.card_m1 = self._make_machine_card_widget(0)
        self.card_m2 = self._make_machine_card_widget(1)

        cards_layout.addWidget(self.card_m1)
        cards_layout.addWidget(self.card_m2)
        lay.addLayout(cards_layout, 1)

        return page

    def _make_machine_card_widget(self, idx):
        m = self.machines[idx]
        frame = QFrame()
        frame.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid {COL_OK if m['status_kalibrasi'] else COL_BAD}; border-radius: 6px;")
        
        flay = QVBoxLayout(frame)
        flay.setAlignment(Qt.AlignCenter)
        flay.setSpacing(4)

        lbl_icon = QLabel("⚙️")
        lbl_icon.setStyleSheet("font-size: 16px; border: none;")
        lbl_icon.setAlignment(Qt.AlignCenter)
        flay.addWidget(lbl_icon)

        lbl_name = QLabel(m["name"])
        lbl_name.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {COL_TEXT_LIGHT}; border: none;")
        lbl_name.setAlignment(Qt.AlignCenter)
        flay.addWidget(lbl_name)

        status_text = "✓ SUDAH KALIBRASI" if m["status_kalibrasi"] else "✕ BELUM KALIBRASI"
        status_col = COL_OK if m["status_kalibrasi"] else COL_BAD
        
        btn_stat = QPushButton(status_text)
        btn_stat.setStyleSheet(f"background-color: transparent; color: {status_col}; font-size: 6px; font-weight: bold; border: 1px solid {status_col}; border-radius: 3px; padding: 3px;")
        btn_stat.clicked.connect(lambda checked, i=idx: self._select_machine_slot(i))
        flay.addWidget(btn_stat)

        return frame

    def _select_machine_slot(self, idx):
        self.selected_machine_idx = idx
        m = self.machines[idx]
        self.btn_slot_selector.setText(f"⚙️  {m['name']}")
        self.lbl_beranda_sub.setText(f"Mesin: {m['name']}")
        self._send_command(m["bearing_cmd"])
        self._change_page(0) # Kembali ke Beranda setelah pilih slot

    # =========================================================================
    # WORKER SERIAL & LOGIKA
    # =========================================================================
    def _init_serial_connection(self):
        if serial is not None:
            t = threading.Thread(target=self._read_serial_worker, daemon=True)
            t.start()

    def _resolve_serial_port(self):
        try:
            ports = list(serial.tools.list_ports.comports())
        except Exception:
            ports = []
        if not ports: 
            return SERIAL_PORT
        available = [p.device for p in ports]
        if SERIAL_PORT in available: 
            return SERIAL_PORT
        for p in ports:
            desc = f"{p.description} {p.manufacturer or ''}".upper()
            if any(hint in desc for hint in ESP32_USB_HINTS):
                return p.device
        return ports[0].device if ports else SERIAL_PORT

    def _read_serial_worker(self):
        while True:
            try:
                if self.ser is None or not self.ser.is_open:
                    port_to_use = self._resolve_serial_port()
                    self.ser = serial.Serial(port_to_use, BAUD_RATE, timeout=0.1)
                    self.serial_connected = True

                raw = self.ser.readline()
                if not raw:
                    if time.time() - self.last_packet_time > 2.5:
                        self.packet_loss_flag = True
                    continue

                self.last_packet_time = time.time()
                self.packet_loss_flag = False

                line = raw.decode('utf-8', errors='ignore').strip()
                if not line.startswith("{"): 
                    continue

                try:
                    data = json.loads(line)
                except (json.JSONDecodeError, ValueError):
                    continue

                self.last_raw_line = line
                self.current_v = float(data.get("rms_v", 0.0))
                self.current_a = float(data.get("rms_a", 0.0))
                self.current_temp = float(data.get("temp", 0.0))
                self.current_rpm = float(data.get("rpm", 0.0))
                self.current_d2 = float(data.get("d2", 0.0))
                self.current_status_device = data.get("status", "normal")
                self.current_servis = data.get("servis_estimasi", "30+ hari")

                self.tick += 1
                status_key = self.current_status_device.strip().lower()

                if status_key == "waspada":
                    self.session_waspada_count += 1
                elif status_key == "bahaya":
                    self.session_bahaya_count += 1

                if status_key in ("waspada", "bahaya"):
                    ts = datetime.now().strftime("%H:%M:%S")
                    event_txt = f"[{ts}] Status {status_key.upper()} — Vib {self.current_v:.2f}G, D² {self.current_d2:.2f}"
                    self.anomaly_events.append(event_txt)

            except Exception as e:
                self.serial_connected = False
                self.ser = None
                time.sleep(1)

    def _send_command(self, cmd_char):
        if self.ser is not None and self.ser.is_open:
            try:
                self.ser.write(cmd_char.encode())
            except Exception as e:
                print(f"Gagal kirim command: {e}")

    def _update_gui(self):
        self.time_lbl.setText(datetime.now().strftime("%H:%M:%S"))
        self.lbl_header_dot.setStyleSheet(
            f"font-size: 9px; color: {COL_OK if (self.serial_connected and not self.packet_loss_flag) else COL_WARN};"
        )

        # Update Tampilan Beranda Berdasarkan Data Live
        if self.selected_machine_idx >= 0:
            st = self.current_status_device.strip().lower()
            if st == "bahaya":
                self.lbl_beranda_icon.setText("⚠️")
                self.lbl_beranda_icon.setStyleSheet(f"font-size: 26px; color: {COL_BAD}; border: none;")
                self.lbl_check_text.setText("BAHAYA / RISIKO KERUSAKAN")
                self.lbl_check_text.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_BAD}; border: none;")
                self.lbl_rec_penyebab.setText("Kemungkinan penyebab: Ketidakseimbangan bearing atau beban berlebih.")
                self.lbl_rec_tindakan.setText("Tindakan yang disarankan: Matikan mesin segera dan lakukan pelumasan/penggantian komponen.")
            elif st == "waspada":
                self.lbl_beranda_icon.setText("⚡")
                self.lbl_beranda_icon.setStyleSheet(f"font-size: 26px; color: {COL_WARN}; border: none;")
                self.lbl_check_text.setText("WASPADA DEVIASI OPERASIONAL")
                self.lbl_check_text.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_WARN}; border: none;")
                self.lbl_rec_penyebab.setText("Kemungkinan penyebab: Gesekan awal atau peningkatan suhu operasional.")
                self.lbl_rec_tindakan.setText("Tindakan yang disarankan: Pantau terus tren getaran dan jadwalkan pemeriksaan rutin.")
            else:
                self.lbl_beranda_icon.setText("✓")
                self.lbl_beranda_icon.setStyleSheet(f"font-size: 26px; color: {COL_OK}; border: none;")
                self.lbl_check_text.setText("MESIN AMAN / NORMAL")
                self.lbl_check_text.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_OK}; border: none;")
                self.lbl_rec_penyebab.setText("Kemungkinan penyebab: Tidak ada anomali terdeteksi.")
                self.lbl_rec_tindakan.setText("Tindakan yang disarankan: Pertahankan jadwal operasional rutin.")

            self.lbl_desc_text.setText(f"Vib: {self.current_v:.2f}G | Snd: {self.current_a:.1f}dB | Temp: {self.current_temp:.1f}°C | D²: {self.current_d2:.2f}")
            self.lbl_estimasi_servis.setText(f"Estimasi servis berikutnya: {self.current_servis}")

        # Update Halaman Riwayat
        self.lbl_stat_waspada.setText(str(self.session_waspada_count))
        self.lbl_stat_bahaya.setText(str(self.session_bahaya_count))
        self.lbl_stat_total.setText(str(self.tick))

        if self.anomaly_events:
            self.list_riwayat.clear()
            for ev in self.anomaly_events[-10:]: # Ambil 10 kejadian terakhir
                self.list_riwayat.addItem(ev)

class GlobalEscHandler(QApplication):
    def notify(self, receiver, event):
        if event.type() == QEvent.KeyPress and event.key() == Qt.Key_Escape:
            for widget in self.topLevelWidgets():
                if isinstance(widget, Dashboard):
                    widget.close()
                    return True
        return super().notify(receiver, event)

if __name__ == '__main__':
    app = GlobalEscHandler(sys.argv)
    db = Dashboard()
    db.show()
    db.raise_()
    db.activateWindow()
    sys.exit(app.exec_())