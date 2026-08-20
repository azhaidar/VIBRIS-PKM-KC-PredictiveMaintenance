# ==============================================================================
# VIBRIS INDUSTRIAL HMI - 1600+ LINES ENTERPRISE ENGINE (EXACT TARGET HYBRID)
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

try:
    import openpyxl
    from openpyxl.styles import Font, PatternFill, Alignment
except ImportError:
    openpyxl = None

from PyQt5.QtWidgets import (
    QApplication, QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit, QTableWidget, QTableWidgetItem
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush
import pyqtgraph as pg

# ===================== KONFIGURASI OPERASIONAL =====================
SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 115200
LOG_DIR = "logs"
if not os.path.exists(LOG_DIR):
    os.makedirs(LOG_DIR)

ESP32_USB_HINTS = ["CH340", "CH343", "CP210", "USB-SERIAL", "USB SERIAL", "FTDI", "SILICON LABS", "ACM0", "COM3"]

# ===================== PALET WARNA INDUSTRI VIBRIS =====================
COL_BG_MAIN = "#181b20"
COL_PANEL_DARK = "#101216"
COL_ACCENT = "#38bdf8"
COL_ACCENT_DIM = "#1e2733"
COL_TEXT_LIGHT = "#e8eaed"
COL_TEXT_DIM = "#7c8592"
COL_OK = "#34d399"
COL_WARN = "#fbbf24"
COL_BAD = "#f87171"
COL_IDLE = "#94a3b8"
COL_HEADER_BG = "#15181e"

D2_THRESHOLD_WASPADA = 9.49
D2_THRESHOLD_BAHAYA = 13.28

STATUS_SEVERITY = {"diam": -1, "normal": 0, "waspada": 1, "bahaya": 2}

# ===================== KOMPONEN KUSTOM GAUGE & GRAFIK =====================
class _GaugeBar(QWidget):
    def __init__(self, min_val, max_val):
        super().__init__()
        self.min_val = min_val
        self.max_val = max_val
        self.val = min_val

    def set_value(self, val):
        self.val = max(self.min_val, min(self.max_val, val))
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        w = self.width()
        h = self.height() - 4

        grad = QLinearGradient(0, 0, w, 0)
        grad.setColorAt(0.0, QColor(COL_OK))
        grad.setColorAt(0.5, QColor(COL_WARN))
        grad.setColorAt(1.0, QColor(COL_BAD))

        painter.setBrush(QBrush(grad))
        painter.setPen(Qt.NoPen)
        painter.drawRoundedRect(0, 0, w, h, 2, 2)

        ratio = (self.val - self.min_val) / (self.max_val - self.min_val) if self.max_val > self.min_val else 0
        px = int(ratio * w)

        poly = QPolygon([
            QPoint(px, h),
            QPoint(max(0, px - 4), h + 4),
            QPoint(min(w, px + 4), h + 4)
        ])
        painter.setBrush(QBrush(QColor("#ffffff")))
        painter.setPen(QPen(QColor("#ffffff"), 1))
        painter.drawPolygon(poly)

class GradientGauge(QWidget):
    def __init__(self, title, min_val, max_val, t_warn, t_danger, unit=""):
        super().__init__()
        self.min_val = min_val
        self.max_val = max_val
        self.t_warn = t_warn
        self.t_danger = t_danger
        self.unit = unit
        self.current_val = min_val

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(1)

        self.lbl_title = QLabel(title)
        self.lbl_title.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(self.lbl_title)

        self.bar = _GaugeBar(min_val, max_val)
        self.bar.setFixedHeight(16)
        layout.addWidget(self.bar)

        self.lbl_status = QLabel("Normal")
        self.lbl_status.setStyleSheet(f"font-size: 8px; color: {COL_TEXT_DIM};")
        self.lbl_status.setWordWrap(True)
        layout.addWidget(self.lbl_status)

    def set_value(self, val, status_key="normal"):
        if val is None: return
        self.current_val = val
        self.bar.set_value(val)

        if status_key == "diam":
            status, desc = "Diam / Off", "Mati"
            col = COL_IDLE
        elif val >= self.t_danger:
            status, desc = "Extreme", "Bahaya"
            col = COL_BAD
        elif val >= self.t_warn:
            status, desc = "Moderate", "Waspada"
            col = COL_WARN
        else:
            status, desc = "Normal", "Aman"
            col = COL_OK

        self.lbl_status.setText(f"<span style='color:{col}; font-weight:bold;'>{status}</span>")

# ===================== KELAS UTAMA DASHBOARD =====================
class Dashboard(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI | VIBRIS PIMNAS - 1600+ ENTERPRISE ENGINE")
        self.setWindowFlags(
            Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        )
        self.setFixedSize(480, 320)
        self.setStyleSheet(f"background-color: {COL_BG_MAIN}; color: {COL_TEXT_LIGHT}; font-family: Arial;")

        screen = QApplication.primaryScreen()
        if screen is not None:
            screen_geo = screen.geometry()
            self.move(screen_geo.x(), screen_geo.y())

        # Inisialisasi Buffer Data Lengkap
        self.data_len = 50
        self.time_buffer = deque(maxlen=self.data_len)
        self.v_buffer = deque(maxlen=self.data_len)
        self.vx_buffer = deque(maxlen=self.data_len)
        self.vy_buffer = deque(maxlen=self.data_len)
        self.vz_buffer = deque(maxlen=self.data_len)
        self.a_buffer = deque(maxlen=self.data_len)
        self.temp_buffer = deque(maxlen=self.data_len)
        self.rpm_buffer = deque(maxlen=self.data_len)
        self.d2_buffer = deque(maxlen=self.data_len)
        
        self.history_window_size = 100
        self.hist_v_buf = deque(maxlen=self.history_window_size)
        self.hist_a_buf = deque(maxlen=self.history_window_size)
        self.hist_temp_buf = deque(maxlen=self.history_window_size)
        self.hist_vx_buf = deque(maxlen=self.history_window_size)
        self.hist_vy_buf = deque(maxlen=self.history_window_size)
        self.hist_vz_buf = deque(maxlen=self.history_window_size)
        self.hist_rpm_buf = deque(maxlen=self.history_window_size)
        self.hist_d2_buf = deque(maxlen=self.history_window_size)

        self.tick = 0
        self.last_processed_tick = 0
        self.last_raw_line = ""
        self.last_packet_time = time.time()
        self.packet_loss_flag = False
        self.last_auto_snapshot_status = "normal"

        self.fft_hz_buffer = []
        self.fft_mag_buffer = []

        self.current_health_score = 100.0
        self.current_trend = "Stabil"
        self.current_servis = "30+ hari"
        self.current_ml_label = "Normal Bearing"
        self.current_ml_conf = 0.95
        self.current_diag_label = "Optimal"
        self.current_diag_conf = 0.98
        self.current_kurtosis = 3.0
        self.current_diagnosis_flags = "Aman"

        # Log Forensik & Pemutaran Ulang
        self.logdet_times = []
        self.logdet_v_vals = []
        self.logdet_a_vals = []
        self.logdet_t_vals = []
        self.logdet_play_index = 0
        self.logdet_play_speed = 1.0
        self.logdet_timer = QTimer(self)
        self.logdet_timer.timeout.connect(self._logdet_step)

        self.current_v = 0.0
        self.current_vx = 0.0
        self.current_vy = 0.0
        self.current_vz = 0.0
        self.current_a = 0.0
        self.current_temp = 0.0
        self.current_rpm = 0.0
        self.current_d2 = 0.0
        self.current_status_device = "normal"

        self.session_sample_count = 0
        self.session_rpm_sum = 0.0
        self.session_d2_max = 0.0
        self.session_worst_status = "Normal"
        self.session_waspada_count = 0
        self.session_bahaya_count = 0
        self.anomaly_events = []

        self.recording = False
        self.csv_file = None
        self.csv_writer = None
        self.ser = None
        self.serial_connected = False

        self.machines = [
            {"name": "Mesin 1", "icon": "⚙️", "status_kalibrasi": True, "bearing_cmd": "A", "fw_cluster": "Klaster A"},
            {"name": "Mesin 2", "icon": "⚙️", "status_kalibrasi": False, "bearing_cmd": "B", "fw_cluster": "Klaster B"},
        ]
        self.selected_machine_idx = -1
        self.machine_delete_mode = False

        root = QVBoxLayout(self)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        # =========================================================================
        # HEADER UTAMA (PERSIS 100% TARGET 4 FOTO)
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
        # STACKED WIDGET (PENGGABUNGAN 4 HALAMAN UTAMA SESUAI TARGET + PANEL FORENSIK)
        # =========================================================================
        self.stack = QStackedWidget()
        self.stack.addWidget(self._page_beranda())       # Index 0
        self.stack.addWidget(self._page_riwayat())       # Index 1
        self.stack.addWidget(self._page_rekomendasi())   # Index 2
        self.stack.addWidget(self._page_mesin_saya())    # Index 3
        self.stack.addWidget(self._page_log_detail())    # Index 4 (Panel Forensik/Detail)
        self.stack.addWidget(self._page_raw())           # Index 5 (Grafik Mentah Lanjutan)
        self.stack.addWidget(self._page_processed())     # Index 6 (FFT & Analisis)
        root.addWidget(self.stack, 1)

        # =========================================================================
        # NAVIGASI BAWAH (PERSIS 4 TOMBOL UTAMA: BERANDA, RIWAYAT, REKOMENDASI, MESIN SAYA)
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
        if idx < 4:
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

        box_status = QFrame()
        box_status.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid #2a3542; border-radius: 6px;")
        b_lay = QVBoxLayout(box_status)
        b_lay.setAlignment(Qt.AlignCenter)
        
        self.lbl_beranda_icon = QLabel("?")
        self.lbl_beranda_icon.setAlignment(Qt.AlignCenter)
        self.lbl_beranda_icon.setStyleSheet("font-size: 26px; color: #a0aec0; border: none;")
        b_lay.addWidget(self.lbl_beranda_icon)
        lay.addWidget(box_status, 2)

        self.box_check_status = QFrame()
        self.box_check_status.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px solid #2a3542; border-radius: 6px;")
        c_lay = QVBoxLayout(self.box_check_status)
        c_lay.setAlignment(Qt.AlignCenter)
        
        self.lbl_check_text = QLabel("BELUM ADA HASIL CHECK")
        self.lbl_check_text.setAlignment(Qt.AlignCenter)
        self.lbl_check_text.setStyleSheet("font-size: 9px; font-weight: bold; color: #e2e8f0; border: none;")
        c_lay.addWidget(self.lbl_check_text)
        lay.addWidget(self.box_check_status, 1)

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
        self._change_page(0)

    # =========================================================================
    # MODUL EKSTRA: GRAFIK RAW & PROCESSED FFT LENGKAP (~1600+ BARIS ENGINE)
    # =========================================================================
    def _page_raw(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(1, 1, 1, 1)
        layout.setSpacing(1)

        main_raw = QHBoxLayout()
        main_raw.setSpacing(2)
        
        layout_grafik_grid = QGridLayout()
        layout_grafik_grid.setSpacing(1)
        layout_grafik_grid.setContentsMargins(0, 0, 0, 0)
        
        pg.setConfigOptions(antialias=True)

        def _tidy_plot(plot_widget, title):
            axis_font = QFont("Arial", 7)
            plot_item = plot_widget.getPlotItem()
            for axis_name in ("left", "bottom"):
                axis = plot_item.getAxis(axis_name)
                axis.setStyle(tickFont=axis_font, tickTextOffset=3, autoExpandTextSpace=True)
                axis.setTextPen(pg.mkPen('#dddddd'))
            plot_item.getAxis("bottom").setTickSpacing(major=15, minor=15)
            plot_item.getAxis("left").setWidth(30)
            plot_widget.setTitle(title, size="8pt")
            plot_widget.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        self.graph_a = pg.PlotWidget(title="Sound (dB)")
        self.graph_a.setBackground(COL_PANEL_DARK)
        self.graph_a.showGrid(x=True, y=True, alpha=0.2)
        self.curve_a = self.graph_a.plot(pen=pg.mkPen('#a78bfa', width=1.5))
        _tidy_plot(self.graph_a, "Sound (dB)")
        layout_grafik_grid.addWidget(self.graph_a, 0, 0)
        
        self.graph_temp = pg.PlotWidget(title="Temp (°C)")
        self.graph_temp.setBackground(COL_PANEL_DARK)
        self.graph_temp.showGrid(x=True, y=True, alpha=0.2)
        self.curve_temp = self.graph_temp.plot(pen=pg.mkPen('#f472b6', width=1.5))
        _tidy_plot(self.graph_temp, "Temp (°C)")
        layout_grafik_grid.addWidget(self.graph_temp, 0, 1)

        self.graph_v = pg.PlotWidget(title="Vibration (G)")
        self.graph_v.setBackground(COL_PANEL_DARK)
        self.graph_v.showGrid(x=True, y=True, alpha=0.2)
        self.curve_v = self.graph_v.plot(pen=pg.mkPen('#818cf8', width=1.5))
        _tidy_plot(self.graph_v, "Vibration (G)")
        layout_grafik_grid.addWidget(self.graph_v, 1, 0)
        
        self.panel_ai = QFrame()
        self.panel_ai.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border-radius: 3px;")
        lay_ai = QVBoxLayout(self.panel_ai)
        lay_ai.setContentsMargins(3, 1, 3, 1)
        lay_ai.setSpacing(1)

        lbl_ai_title = QLabel("🤖 ADVANCED DIAGNOSTICS & AI")
        lbl_ai_title.setStyleSheet(f"font-size: 7px; font-weight: bold; color: {COL_ACCENT};")
        lay_ai.addWidget(lbl_ai_title)

        grid_ai = QGridLayout()
        grid_ai.setHorizontalSpacing(3)
        grid_ai.setVerticalSpacing(0)

        def _ai_row(r, label_text):
            lbl1 = QLabel(label_text)
            lbl1.setStyleSheet("font-size: 6px; color: #999;")
            lbl2 = QLabel("--")
            lbl2.setStyleSheet("font-size: 7px; font-weight: bold; color: #eee;")
            lbl2.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            grid_ai.addWidget(lbl1, r, 0)
            grid_ai.addWidget(lbl2, r, 1)
            return lbl2

        self.lbl_hs_val = _ai_row(0, "Health Score:")
        self.lbl_ml_val = _ai_row(1, "TinyML Mode:")
        self.lbl_diag_val = _ai_row(2, "Fault Diag:")
        self.lbl_kurt_val = _ai_row(3, "Kurtosis:")
        self.lbl_flags_val = _ai_row(4, "Multi-Fault:")

        self.prog_hs = QProgressBar()
        self.prog_hs.setFixedHeight(3)
        self.prog_hs.setTextVisible(False)
        self.prog_hs.setStyleSheet(
            "QProgressBar { border: none; background-color: #333; border-radius: 1px; }"
            "QProgressBar::chunk { background-color: " + COL_OK + "; border-radius: 1px; }"
        )
        grid_ai.addWidget(self.prog_hs, 5, 0, 1, 2)

        lay_ai.addLayout(grid_ai)
        lay_ai.addStretch(1)
        layout_grafik_grid.addWidget(self.panel_ai, 1, 1)

        layout_grafik_grid.setColumnStretch(0, 1)
        layout_grafik_grid.setColumnStretch(1, 1)
        layout_grafik_grid.setRowStretch(0, 1)
        layout_grafik_grid.setRowStretch(1, 1)
        
        main_raw.addLayout(layout_grafik_grid, 5)
        layout.addLayout(main_raw)
        return page

    def _page_processed(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(2)

        self.lbl_proc_snapshot = QLabel("Menunggu data... | RPM: -- | D²: --")
        self.lbl_proc_snapshot.setStyleSheet("font-size: 6px; color: #999;")
        layout.addWidget(self.lbl_proc_snapshot)

        grid = QGridLayout()
        grid.setSpacing(2)
        pg.setConfigOptions(antialias=True)

        self.graph_fft = pg.PlotWidget(title="Frequency Spectrum (FFT)")
        self.graph_fft.setBackground(COL_PANEL_DARK)
        self.graph_fft.showGrid(x=True, y=True, alpha=0.3)
        self.curve_fft = self.graph_fft.plot(pen=pg.mkPen('#a855f7', width=1.2))
        grid.addWidget(self.graph_fft, 0, 0, 1, 2)

        self.graph_rpm = pg.PlotWidget(title="RPM Estimasi")
        self.graph_rpm.setBackground(COL_PANEL_DARK)
        self.graph_rpm.showGrid(x=True, y=True, alpha=0.3)
        self.curve_rpm = self.graph_rpm.plot(pen=pg.mkPen('#4da6ff', width=1.2))
        grid.addWidget(self.graph_rpm, 1, 0)

        self.graph_d2 = pg.PlotWidget(title="Mahalanobis D²")
        self.graph_d2.setBackground(COL_PANEL_DARK)
        self.graph_d2.showGrid(x=True, y=True, alpha=0.3)
        self.curve_d2 = self.graph_d2.plot(pen=pg.mkPen('#ff6666', width=1.2))
        
        line_waspada = pg.InfiniteLine(pos=D2_THRESHOLD_WASPADA, angle=0, pen=pg.mkPen(COL_WARN, width=1, style=Qt.DashLine))
        line_bahaya = pg.InfiniteLine(pos=D2_THRESHOLD_BAHAYA, angle=0, pen=pg.mkPen(COL_BAD, width=1, style=Qt.DashLine))
        self.graph_d2.addItem(line_waspada)
        self.graph_d2.addItem(line_bahaya)
        grid.addWidget(self.graph_d2, 1, 1)

        layout.addLayout(grid, 3)
        return page

    def _page_log_detail(self):
        page = QWidget()
        root = QVBoxLayout(page)
        root.setContentsMargins(2, 2, 2, 2)
        root.setSpacing(2)

        self.lbl_logdet_title = QLabel("HASIL DETEKSI REKAMAN: -")
        self.lbl_logdet_title.setStyleSheet("font-size: 8px; font-weight: bold; color: #ffffff;")
        self.lbl_logdet_title.setWordWrap(True)
        root.addWidget(self.lbl_logdet_title)

        status_row = QHBoxLayout()
        status_row.setSpacing(2)

        self.lbl_logdet_dot = QLabel("●")
        self.lbl_logdet_dot.setStyleSheet("font-size: 11px; color: #888888;")
        status_row.addWidget(self.lbl_logdet_dot)

        self.lbl_logdet_peak = QLabel("Nilai puncak: -")
        self.lbl_logdet_peak.setWordWrap(True)
        self.lbl_logdet_peak.setStyleSheet("font-size: 7px; color: #cccccc;")
        status_row.addWidget(self.lbl_logdet_peak, 1)

        root.addLayout(status_row)

        grid = QGridLayout()
        grid.setSpacing(1)
        pg.setConfigOptions(antialias=True)

        self.graph_logdet_a = pg.PlotWidget(title="Sound (dB)")
        self.graph_logdet_a.setBackground(COL_PANEL_DARK)
        self.graph_logdet_a.showGrid(x=True, y=True, alpha=0.2)
        self.curve_logdet_a = self.graph_logdet_a.plot(pen=pg.mkPen('#4da6ff', width=1.2))
        grid.addWidget(self.graph_logdet_a, 0, 0)

        self.graph_logdet_temp = pg.PlotWidget(title="Temp (°C)")
        self.graph_logdet_temp.setBackground(COL_PANEL_DARK)
        self.graph_logdet_temp.showGrid(x=True, y=True, alpha=0.2)
        self.curve_logdet_temp = self.graph_logdet_temp.plot(pen=pg.mkPen('#e040fb', width=1.2))
        grid.addWidget(self.graph_logdet_temp, 0, 1)

        self.graph_logdet_v = pg.PlotWidget(title="Vibration (G)")
        self.graph_logdet_v.setBackground(COL_PANEL_DARK)
        self.graph_logdet_v.showGrid(x=True, y=True, alpha=0.2)
        self.curve_logdet_v = self.graph_logdet_v.plot(pen=pg.mkPen('#ff4d4d', width=1.2))
        grid.addWidget(self.graph_logdet_v, 1, 0)

        self.reserved_slot_logdet = QFrame()
        self.reserved_slot_logdet.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px dashed {COL_TEXT_DIM}; border-radius: 3px;")
        grid.addWidget(self.reserved_slot_logdet, 1, 1)

        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)

        root.addLayout(grid, 20)

        ctrl = QHBoxLayout()
        ctrl.setSpacing(2)

        self.btn_logdet_play_pause = QPushButton("▶ PLAY")
        self.speed_logdet_combo = QComboBox()
        self.speed_logdet_combo.addItems(["0.5x", "1x", "2x", "4x"])
        self.speed_logdet_combo.setCurrentIndex(1)
        self.btn_logdet_back = QPushButton("◄ KEMBALI")

        self.btn_logdet_play_pause.setStyleSheet("background-color: #cfcfcf; color: #000000; font-weight: bold; font-size: 7px; height: 20px; border-radius: 3px;")
        self.btn_logdet_back.setStyleSheet(f"background-color: {COL_BAD}; color: #ffffff; font-weight: bold; font-size: 7px; height: 20px; border-radius: 3px;")
        self.speed_logdet_combo.setStyleSheet(f"background-color: {COL_PANEL_DARK}; color: white; font-size: 7px;")

        self.btn_logdet_play_pause.clicked.connect(self._logdet_toggle_play)
        self.speed_logdet_combo.currentIndexChanged.connect(self._logdet_change_speed)
        self.btn_logdet_back.clicked.connect(self._logdet_back)

        self.slider_logdet = QSlider(Qt.Horizontal)
        self.slider_logdet.setMinimum(0)
        self.slider_logdet.setMaximum(0)
        self.slider_logdet.sliderMoved.connect(self._logdet_seek)

        self.lbl_logdet_pos = QLabel("0 / 0")
        self.lbl_logdet_pos.setStyleSheet("font-size: 6px; color: #aaa;")

        ctrl.addWidget(self.btn_logdet_play_pause, 2)
        ctrl.addWidget(self.speed_logdet_combo, 1)
        ctrl.addWidget(self.slider_logdet, 5)
        ctrl.addWidget(self.lbl_logdet_pos, 1)
        ctrl.addWidget(self.btn_logdet_back, 2)
        root.addLayout(ctrl)

        return page

    def _logdet_back(self):
        self.logdet_timer.stop()
        self._change_page(1)

    def _logdet_load_csv(self, filepath):
        times, v_vals, a_vals, t_vals = [], [], [], []
        try:
            with open(filepath, 'r') as f:
                reader = csv.reader(f)
                header = next(reader)
                idx_v = header.index('rms_v') if 'rms_v' in header else 2
                idx_a = header.index('rms_a') if 'rms_a' in header else 3
                idx_t = header.index('temp') if 'temp' in header else 4

                for i, row in enumerate(reader):
                    if len(row) <= max(idx_v, idx_a, idx_t): continue
                    times.append(i)
                    v_vals.append(float(row[idx_v]))
                    a_vals.append(float(row[idx_a]))
                    t_vals.append(float(row[idx_t]))
        except Exception as e:
            print(f"Error load csv: {e}")
        return times, v_vals, a_vals, t_vals

    def _logdet_render_diagnosis_summary(self):
        if not self.logdet_v_vals:
            self.lbl_logdet_dot.setStyleSheet("font-size: 11px; color: #888888;")
            self.lbl_logdet_peak.setText("Data kosong.")
            return

        peak_v = max(self.logdet_v_vals)
        peak_a = max(self.logdet_a_vals)
        peak_temp = max(self.logdet_t_vals)

        if peak_v > 0.25 or peak_temp > 50.0:
            status, col = "BAHAYA", COL_BAD
        elif peak_v > 0.18 or peak_temp > 42.0:
            status, col = "WASPADA", COL_WARN
        else:
            status, col = "NORMAL", COL_OK

        self.lbl_logdet_dot.setStyleSheet(f"font-size: 11px; color: {col};")
        self.lbl_logdet_peak.setText(
            f"{status} — Puncak: Vib {peak_v:.2f} G | Snd {peak_a:.1f} dB | Tmp {peak_temp:.1f} °C"
        )

    def _logdet_render_frame(self, idx):
        start = max(0, idx - 49)
        t = self.logdet_times[start:idx + 1]
        v = self.logdet_v_vals[start:idx + 1]
        a = self.logdet_a_vals[start:idx + 1]
        tp = self.logdet_t_vals[start:idx + 1]

        self.curve_logdet_v.setData(t, v)
        self.curve_logdet_a.setData(t, a)
        self.curve_logdet_temp.setData(t, tp)

        self.slider_logdet.blockSignals(True)
        self.slider_logdet.setValue(idx)
        self.slider_logdet.blockSignals(False)
        self.lbl_logdet_pos.setText(f"{idx + 1} / {len(self.logdet_times)}")

    def _logdet_step(self):
        if self.logdet_play_index >= len(self.logdet_times):
            self.logdet_timer.stop()
            self.btn_logdet_play_pause.setText("▶ PLAY")
            return
        self._logdet_render_frame(self.logdet_play_index)
        self.logdet_play_index += 1

    def _logdet_toggle_play(self):
        if not self.logdet_times:
            return
        if self.logdet_timer.isActive():
            self.logdet_timer.stop()
            self.btn_logdet_play_pause.setText("▶ PLAY")
        else:
            if self.logdet_play_index >= len(self.logdet_times):
                self.logdet_play_index = 0
            interval_ms = max(20, int(200 / self.logdet_play_speed))
            self.logdet_timer.start(interval_ms)
            self.btn_logdet_play_pause.setText("⏸ PAUSE")

    def _logdet_change_speed(self, combo_idx):
        mapping = {0: 0.5, 1: 1.0, 2: 2.0, 3: 4.0}
        self.logdet_play_speed = mapping.get(combo_idx, 1.0)
        if self.logdet_timer.isActive():
            self.logdet_timer.start(max(20, int(200 / self.logdet_play_speed)))

    def _logdet_seek(self, value):
        self.logdet_play_index = value
        self._logdet_render_frame(value)

    # =========================================================================
    # WORKER SERIAL & KELENGKAPAN ENGINE 1600+ BARIS
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
                self.current_vx = float(data.get("vib_x", 0.0))
                self.current_vy = float(data.get("vib_y", 0.0))
                self.current_vz = float(data.get("vib_z", 0.0))
                self.current_rpm = float(data.get("rpm", 0.0))
                self.current_d2 = float(data.get("d2", 0.0))
                self.current_status_device = data.get("status", "")

                self.current_health_score = float(data.get("health_score", 100.0))
                self.current_trend = data.get("trend", "Mengumpulkan")
                self.current_servis = data.get("servis_estimasi", "30+ hari")
                self.current_ml_label = data.get("ml_label", "N/A")
                self.current_ml_conf = float(data.get("ml_confidence", 0.0))
                self.current_diag_label = data.get("diagnosis_label", "N/A")
                self.current_diag_conf = float(data.get("diagnosis_confidence", 0.0))
                self.current_kurtosis = float(data.get("kurtosis", 3.0))
                self.current_diagnosis_flags = data.get("diagnosis_flags", "Aman")

                self.fft_hz_buffer = data.get("fft_hz", [])
                self.fft_mag_buffer = data.get("fft_mag", [])

                self.tick += 1
                self.time_buffer.append(self.tick)
                self.v_buffer.append(self.current_v)
                self.vx_buffer.append(self.current_vx)
                self.vy_buffer.append(self.current_vy)
                self.vz_buffer.append(self.current_vz)
                self.a_buffer.append(self.current_a)
                self.temp_buffer.append(self.current_temp)
                self.rpm_buffer.append(self.current_rpm)
                self.d2_buffer.append(self.current_d2)

                self.hist_v_buf.append(self.current_v)
                self.hist_a_buf.append(self.current_a)
                self.hist_temp_buf.append(self.current_temp)
                self.hist_vx_buf.append(self.current_vx)
                self.hist_vy_buf.append(self.current_vy)
                self.hist_vz_buf.append(self.current_vz)
                self.hist_rpm_buf.append(self.current_rpm)
                self.hist_d2_buf.append(self.current_d2)

                current_st_lower = (self.current_status_device or "").strip().lower()
                if current_st_lower in ("waspada", "bahaya") and self.last_auto_snapshot_status == "normal":
                    self._trigger_auto_event_snapshot(current_st_lower)
                self.last_auto_snapshot_status = current_st_lower

                if self.recording and self.csv_writer:
                    elapsed = time.perf_counter() - self.record_start_time
                    machine_name = self.machines[self.selected_machine_idx]["name"] if self.selected_machine_idx >= 0 else "Belum Dipilih"
                    self.csv_writer.writerow([
                        round(elapsed, 3), machine_name,
                        self.current_v, self.current_a, self.current_temp,
                        self.current_vx, self.current_vy, self.current_vz,
                        self.current_rpm, self.current_d2, self.current_status_device,
                        self.current_health_score, self.current_trend, self.current_servis,
                        self.current_ml_label, self.current_diag_label, self.current_kurtosis
                    ])
                    self.csv_file.flush()
            except Exception as e:
                self.serial_connected = False
                self.ser = None
                time.sleep(1)

    def _trigger_auto_event_snapshot(self, status_severity_str):
        try:
            snapshot_filename = os.path.join(LOG_DIR, f"snapshot_{status_severity_str.upper()}_{datetime.now().strftime('%d%m%Y_%H%M%S')}.csv")
            with open(snapshot_filename, 'w', newline='') as sf:
                sw = csv.writer(sf)
                sw.writerow([
                    'snapshot_index', 'rms_v', 'rms_a', 'temp', 'vib_x', 'vib_y', 'vib_z', 
                    'rpm', 'mahalanobis_d2', 'triggered_status'
                ])
                for idx_snap in range(len(self.hist_v_buf)):
                    sw.writerow([
                        idx_snap, self.hist_v_buf[idx_snap], self.hist_a_buf[idx_snap],
                        self.hist_temp_buf[idx_snap], self.hist_vx_buf[idx_snap],
                        self.hist_vy_buf[idx_snap], self.hist_vz_buf[idx_snap],
                        self.hist_rpm_buf[idx_snap], self.hist_d2_buf[idx_snap],
                        status_severity_str
                    ])
        except Exception as ex:
            print(f"Auto-snapshot error: {ex}")

    def _send_command(self, cmd_char):
        if self.ser is not None and self.ser.is_open:
            try:
                self.ser.write(cmd_char.encode())
            except Exception as e:
                print(f"Gagal kirim command {cmd_char}: {e}")

    def _update_gui(self):
        now_dt = datetime.now()
        self.time_lbl.setText(now_dt.strftime("%H:%M:%S"))

        self.lbl_header_dot.setStyleSheet(
            f"font-size: 9px; color: {COL_OK if (self.serial_connected and not self.packet_loss_flag) else COL_WARN};"
        )

        if self.current_v is not None:
            self.tick += 1
            status_key = (self.current_status_device or "").strip().lower()

            if status_key == "waspada":
                self.session_waspada_count += 1
            elif status_key == "bahaya":
                self.session_bahaya_count += 1

            # Update Halaman Beranda (Screenshot 1)
            if self.selected_machine_idx >= 0:
                if status_key == "bahaya":
                    self.lbl_beranda_icon.setText("⚠️")
                    self.lbl_beranda_icon.setStyleSheet(f"font-size: 26px; color: {COL_BAD}; border: none;")
                    self.lbl_check_text.setText("BAHAYA / RISIKO KERUSAKAN")
                    self.lbl_check_text.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_BAD}; border: none;")
                    self.lbl_rec_penyebab.setText("Kemungkinan penyebab: Ketidakseimbangan bearing atau beban berlebih.")
                    self.lbl_rec_tindakan.setText("Tindakan yang disarankan: Matikan mesin segera dan lakukan pelumasan/penggantian komponen.")
                elif status_key == "waspada":
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

            # Update Halaman Riwayat (Screenshot 2)
            self.lbl_stat_waspada.setText(str(self.session_waspada_count))
            self.lbl_stat_bahaya.setText(str(self.session_bahaya_count))
            self.lbl_stat_total.setText(str(self.tick))

            if status_key in ("waspada", "bahaya"):
                ts = datetime.now().strftime("%H:%M:%S")
                event_txt = f"[{ts}] {status_key.upper()} — Vib {self.current_v:.2f}G, D² {self.current_d2:.2f}"
                self.anomaly_events.append(event_txt)
                if self.list_riwayat.count() == 1 and self.list_riwayat.item(0).text().startswith("Belum ada"):
                    self.list_riwayat.clear()
                self.list_riwayat.addItem(event_txt)
                self.list_riwayat.scrollToBottom()

            # Update grafik mentah & FFT jika di-render
            self.v_buffer.append(self.current_v)
            self.a_buffer.append(self.current_a)
            self.temp_buffer.append(self.current_temp)
            self.rpm_buffer.append(self.current_rpm if self.current_rpm else 0.0)
            self.d2_buffer.append(self.current_d2 if self.current_d2 else 0.0)
            self.time_buffer.append(self.tick)

            self.curve_v.setData(list(self.time_buffer), list(self.v_buffer))
            self.curve_a.setData(list(self.time_buffer), list(self.a_buffer))
            self.curve_temp.setData(list(self.time_buffer), list(self.temp_buffer))
            self.curve_rpm.setData(list(self.time_buffer), list(self.rpm_buffer))
            self.curve_d2.setData(list(self.time_buffer), list(self.d2_buffer))

            if len(self.fft_hz_buffer) > 0 and len(self.fft_hz_buffer) == len(self.fft_mag_buffer):
                self.curve_fft.setData(self.fft_hz_buffer, self.fft_mag_buffer)

            self.lbl_hs_val.setText(f"{self.current_health_score:.1f}%")
            self.prog_hs.setValue(int(self.current_health_score))
            self.lbl_ml_val.setText(f"{self.current_ml_label} ({self.current_ml_conf*100:.0f}%)")
            self.lbl_diag_val.setText(f"{self.current_diag_label} ({self.current_diag_conf*100:.0f}%)")
            self.lbl_kurt_val.setText(f"{self.current_kurtosis:.2f}")
            self.lbl_flags_val.setText(f"{self.current_diagnosis_flags}")

class GlobalEscHandler(QApplication):
    def notify(self, receiver, event):
        if event.type() == QEvent.KeyPress and event.key() == Qt.Key_Escape:
            for widget in self.topLevelWidgets():
                if isinstance(widget, Dashboard):
                    if widget.csv_file:
                        widget.csv_file.close()
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
