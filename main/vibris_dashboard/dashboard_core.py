from PyQt5.QtWidgets import (
    QApplication, QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush
import pyqtgraph as pg
import time
from collections import deque
from datetime import datetime

from config import (
    COL_BG_MAIN, COL_PANEL_DARK, COL_ACCENT, COL_ACCENT_DIM, COL_TEXT_LIGHT,
    COL_TEXT_DIM, COL_OK, COL_WARN, COL_BAD, COL_IDLE, COL_HEADER_BG,
    BAUD_RATE, STATUS_SEVERITY, SLOT_DEFS,
)
from widgets.gauge import GradientGauge
from serial_worker import SerialWorkerMixin
from pages.page_raw import RawPageMixin
from pages.page_recording import RecordingPageMixin
from pages.page_processed import ProcessedPageMixin
from pages.page_summary import SummaryPageMixin
from pages.page_machine_select import MachineSelectMixin
from pages.page_log_detail import LogDetailMixin
from pages.page_awam import AwamPagesMixin
from pages.page_flow import FlowMixin

# ===================== KELAS UTAMA DASHBOARD =====================
class Dashboard(
    QWidget,
    SerialWorkerMixin,
    RawPageMixin,
    RecordingPageMixin,
    ProcessedPageMixin,
    SummaryPageMixin,
    MachineSelectMixin,
    LogDetailMixin,
    AwamPagesMixin,
    FlowMixin,
):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI | Rotating Machinery Detection System (PIMNAS Ready)")
        self.setWindowFlags(
            Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
        )
        self.setFixedSize(480, 320)
        self.setStyleSheet(f"background-color: {COL_BG_MAIN}; color: {COL_TEXT_LIGHT}; font-family: Arial;")

        screen = QApplication.primaryScreen()
        if screen is not None:
            screen_geo = screen.geometry()
            self.move(screen_geo.x(), screen_geo.y())

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

        self.current_health_score = 0.0
        self.current_trend = "Menunggu"
        self.current_servis = "Menunggu"
        self.current_ml_label = "N/A"
        self.current_ml_conf = 0.0
        self.current_diag_label = "N/A"
        self.current_diag_conf = 0.0
        self.current_kurtosis = 0.0
        self.current_diagnosis_flags = "N/A"

        self.logdet_times = []
        self.logdet_v_vals = []
        self.logdet_a_vals = []
        self.logdet_t_vals = []
        self.logdet_play_index = 0
        self.logdet_play_speed = 1.0
        self.logdet_timer = QTimer(self)
        self.logdet_timer.timeout.connect(self._logdet_step)

        self.current_v = None
        self.current_vx = None
        self.current_vy = None
        self.current_vz = None
        self.current_a = None
        self.current_temp = None
        self.current_rpm = None
        self.current_d2 = None
        self.current_status_device = ""

        self.session_sample_count = 0
        self.session_rpm_sum = 0.0
        self.session_d2_max = 0.0
        self.session_worst_status = "Normal"
        self.session_waspada_count = 0
        self.session_bahaya_count = 0
        self.session_diam_count = 0
        self.anomaly_events = []

        self.recording = False
        self.csv_file = None
        self.csv_writer = None
        self.ser = None
        self.serial_connected = False

        # Slot mesin: cuma 2, tetap (Slot A / Slot B), sesuai SLOT_DEFS di
        # config.py -- gak ada lagi nambah/hapus/nama custom mesin, soalnya
        # device ini gak punya keyboard dan motor yang beneran ada cuma 2
        # klaster bearing (lihat diskusi & page_flow.py).
        self.selected_slot_idx = -1
        # True cuma selama jendela waktu sesi Check (60 detik) lagi jalan --
        # dipakai buat nge-gate hitungan "Total Data Masuk" di _update_gui
        # supaya angkanya BERHENTI nambah pas sesi Check-nya selesai, gak
        # numpuk tanpa henti kayak sebelumnya.
        self.check_active = False
        # Timer buat countdown Loading/Kalibrasi/Check -- dikelola di
        # page_flow.py, disimpan di sini biar timer sebelumnya bisa
        # dihentikan sebelum bikin yang baru.
        self.flow_timer = None
        # Hasil sesi Check TERAKHIR per slot ({0: {...}, 1: {...}}, slot yang
        # belum pernah di-Check gak punya entry). Halaman Beranda/Rekomendasi
        # (mode Awam) SENGAJA dibaca dari sini, BUKAN dari data live yang
        # terus mengalir dari ESP32 -- supaya "Check" beneran berarti
        # pencet, tunggu 1 menit, baru keluar hasil. Diisi oleh
        # _finish_check() di page_flow.py, dibaca oleh _render_beranda() di
        # bawah. DISIMPAN KE DISK (lihat _load_last_check_results) supaya
        # hasil Check terakhir gak "hilang" cuma gara-gara aplikasi
        # dashboard-nya ditutup/dibuka lagi -- beda dari dulu yang cuma
        # nyimpen di RAM.
        self.last_check_results = {}
        self._load_last_check_results()
        # Halaman mana yang lagi "jalan di latar belakang" -- None kalau
        # gak ada Loading/Kalibrasi/Check yang aktif. Dipakai supaya klik
        # header (lbl_machine_active) bisa lompat balik ke proses yang
        # sedang berjalan, bukan cuma selalu ke halaman pilih slot.
        self._active_flow_page = None
        # Diisi 'calibrate' atau 'check' waktu user pencet tombol di
        # halaman Pilih Aksi (lihat _user_start_kalibrasi/_user_start_check
        # di page_flow.py) -- dibaca _proceed_pending_action() begitu
        # Loading (5 detik settle) kelar, buat tau mau jalanin yang mana.
        self._pending_action = None
        self._load_slot_calib_state()

        # ===== Mode tampilan: AWAM (pemilik UMKM) vs TEKNISI (mahasiswa/servis) =====
        # Default AWAM karena target penggunanya adalah pemilik mesin, bukan
        # teknisi. nav_map isinya (label_tombol, index_halaman_di_stack) --
        # dipakai supaya 4 tombol nav bawah bisa "diisi ulang" beda-beda per
        # mode tanpa harus bikin 2 set widget tombol yang terpisah.
        self.ui_mode = "awam"
        self.nav_map_teknisi = [
            ("RAW READING", 0), ("LOGS SAVES", 1),
            ("PROCESSED (FFT)", 2), ("SUMMARY", 3),
        ]
        self.nav_map_awam = [
            ("BERANDA", 6), ("RIWAYAT", 7),
            ("REKOMENDASI", 8), ("MESIN SAYA", 4),
        ]
        self.nav_map = self.nav_map_awam

        root = QVBoxLayout(self)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        header = QHBoxLayout()
        header.setSpacing(2)
        header.setContentsMargins(4, 2, 4, 2)
        self.header_frame = QFrame()
        self.header_frame.setStyleSheet(
            f"background-color: {COL_HEADER_BG}; border-radius: 4px; "
            f"border-bottom: 2px solid {COL_ACCENT};"
        )
        self.header_frame.setLayout(header)

        self.lbl_machine_active = QPushButton("⚙️  - Pilih Slot -")
        self.lbl_machine_active.setStyleSheet(
            f"QPushButton {{ background-color: {COL_ACCENT_DIM}; color: {COL_TEXT_LIGHT}; font-size: 9px; "
            f"font-weight: bold; padding: 2px 4px; border: 1px solid #2a3542; border-radius: 3px; text-align: left; }}"
            f"QPushButton:hover {{ border: 1px solid {COL_ACCENT}; }}"
        )
        # Kalau ada Loading/Kalibrasi/Check yang lagi jalan di latar
        # belakang, klik header ini lompat ke layar proses itu (biar user
        # bisa cek progress kapan aja). Kalau gak ada, klik header ke
        # halaman pilih slot seperti biasa. Lihat _on_machine_active_clicked
        # di page_flow.py.
        self.lbl_machine_active.clicked.connect(self._on_machine_active_clicked)
        header.addWidget(self.lbl_machine_active, 2)

        self.btn_mode_toggle = QPushButton("MODE: AWAM")
        self.btn_mode_toggle.setStyleSheet(
            f"QPushButton {{ background-color: {COL_ACCENT}; color: #000000; font-weight: bold; "
            f"font-size: 9px; padding: 2px 4px; border-radius: 3px; }}"
        )
        self.btn_mode_toggle.clicked.connect(self._toggle_ui_mode)
        header.addWidget(self.btn_mode_toggle)

        def _ghost_btn_style(border_color, text_color):
            return (
                f"QPushButton {{ background-color: transparent; color: {text_color}; "
                f"font-weight: bold; font-size: 9px; padding: 2px 4px; "
                f"border: 1px solid {border_color}; border-radius: 3px; }}"
                f"QPushButton:hover {{ background-color: {border_color}; color: #101216; }}"
            )

        self.btn_cek1m = QPushButton("SESI(K)")
        self.btn_cek1m.setStyleSheet(_ghost_btn_style(COL_OK, COL_OK))
        # Manual trigger buat Teknisi -- pakai flow yang SAMA dengan alur
        # tuntunan otomatis (_start_check_flow di page_flow.py), jadi
        # perilakunya konsisten: counter Riwayat reset & berhenti pas 60
        # detik habis, gak peduli dipicu manual atau lewat pilih slot.
        self.btn_cek1m.clicked.connect(self._start_check_flow)
        header.addWidget(self.btn_cek1m)

        self.btn_slot_res = QPushButton("SLOT(P)")
        self.btn_slot_res.setStyleSheet(_ghost_btn_style(COL_ACCENT, COL_ACCENT))
        self.btn_slot_res.clicked.connect(lambda: self._send_command('P'))
        header.addWidget(self.btn_slot_res)

        self.btn_del_base = QPushButton("BASE(Z)")
        self.btn_del_base.setStyleSheet(_ghost_btn_style(COL_WARN, COL_WARN))
        self.btn_del_base.clicked.connect(self._confirm_delete_baseline)
        header.addWidget(self.btn_del_base)

        self.btn_recal = QPushButton("KALIBRASI")
        self.btn_recal.setStyleSheet(_ghost_btn_style(COL_ACCENT, COL_ACCENT))
        self.btn_recal.clicked.connect(lambda: self._send_command('R'))
        header.addWidget(self.btn_recal)

        self.btn_reboot_esp = QPushButton("REBOOT")
        self.btn_reboot_esp.setStyleSheet(_ghost_btn_style(COL_BAD, COL_BAD))
        self.btn_reboot_esp.clicked.connect(self._confirm_reboot_esp)
        header.addWidget(self.btn_reboot_esp)

        self.btn_debug = QPushButton("DEBUG")
        self.btn_debug.setStyleSheet(_ghost_btn_style(COL_TEXT_DIM, COL_TEXT_DIM))
        self.btn_debug.clicked.connect(self._show_debug_info)
        header.addWidget(self.btn_debug)

        self.lbl_conn_dot = QLabel("●")
        self.lbl_conn_dot.setStyleSheet(f"font-size: 10px; font-weight: bold; color: {COL_BAD};")
        header.addWidget(self.lbl_conn_dot)

        # Widget Jam & Tanggal Real-Time di Header Pojok Kanan
        time_box = QVBoxLayout()
        time_box.setSpacing(0)
        time_box.setContentsMargins(0, 0, 0, 0)

        self.time_lbl = QLabel("--:--:--")
        self.time_lbl.setStyleSheet(f"font-size: 9px; font-weight: bold; color: {COL_OK};")
        self.time_lbl.setAlignment(Qt.AlignRight)
        time_box.addWidget(self.time_lbl)

        self.date_lbl = QLabel("--/--/----")
        self.date_lbl.setStyleSheet(f"font-size: 6px; color: {COL_OK};")
        self.date_lbl.setAlignment(Qt.AlignRight)
        time_box.addWidget(self.date_lbl)

        header.addLayout(time_box)
        root.addWidget(self.header_frame)

        self.stack = QStackedWidget()
        self.stack.addWidget(self._page_raw())              # index 0
        self.stack.addWidget(self._page_recording())        # index 1
        self.stack.addWidget(self._page_processed())        # index 2
        self.stack.addWidget(self._page_summary())          # index 3
        self.stack.addWidget(self._page_machine_select())   # index 4
        self.stack.addWidget(self._page_log_detail())        # index 5
        self.stack.addWidget(self._page_beranda())           # index 6 (mode Awam)
        self.stack.addWidget(self._page_riwayat())            # index 7 (mode Awam)
        self.stack.addWidget(self._page_rekomendasi())        # index 8 (mode Awam)
        self.stack.addWidget(self._page_loading())            # index 9  (alur tuntunan)
        self.stack.addWidget(self._page_calibrating())        # index 10 (alur tuntunan)
        self.stack.addWidget(self._page_checking())           # index 11 (alur tuntunan)
        self.stack.addWidget(self._page_hasil_kalibrasi())    # index 12 (alur tuntunan)
        self.stack.addWidget(self._page_hasil_check())        # index 13 (alur tuntunan)
        self.stack.addWidget(self._page_pilih_aksi())          # index 14 (pilih Kalibrasi/Check manual)
        root.addWidget(self.stack, 1)

        nav_bottom = QHBoxLayout()
        nav_bottom.setSpacing(4)

        self.btn_raw = QPushButton("RAW READING")
        self.btn_rec = QPushButton("LOGS SAVES")
        self.btn_proc = QPushButton("PROCESSED (FFT)")
        self.btn_sum = QPushButton("SUMMARY")

        self.menu_buttons = [self.btn_raw, self.btn_rec, self.btn_proc, self.btn_sum]

        # Tombol nav gak lagi diikat langsung ke satu halaman tetap. Tiap
        # tombol cuma "posisi ke-i" -- halaman tujuannya dibaca dari
        # self.nav_map saat diklik, jadi teks & tujuan tombol bisa diganti
        # total pas mode di-toggle (lihat _apply_ui_mode) tanpa bongkar ulang
        # koneksi sinyal/slot-nya.
        for i, btn in enumerate(self.menu_buttons):
            btn.setFixedHeight(38)
            btn.setStyleSheet(self._menu_style(False))
            btn.clicked.connect(lambda checked, pos=i: self._change_page(self.nav_map[pos][1]))
            nav_bottom.addWidget(btn)

        root.addLayout(nav_bottom)

        self._init_serial_connection()

        self.main_timer = QTimer(self)
        self.main_timer.timeout.connect(self._update_gui)
        self.main_timer.start(200)

        self._apply_ui_mode()
        self._refresh_log_list()
        self._render_beranda()  # tampilkan hasil Check terakhir yang tersimpan (kalau ada), atau placeholder
        self.show()


    def _slot_display_name(self, idx):
        """Nama mesin yang ditampilkan ke USER. Di mode Awam SENGAJA cuma
        nama slot polos (mis. "Slot A") -- istilah "Klaster (~1400 RPM)"
        itu jargon teknis buat hitung frekuensi fault bearing, gak relevan
        buat pemilik UMKM yang cuma mau tahu "mesin yang mana". Di mode
        Teknisi tetap ditampilkan lengkap karena info klaster itu memang
        dibutuhkan teknisi buat diagnosa. SATU tempat ini yang nentuin --
        semua halaman (header, Kalibrasi, Beranda, dst) manggil method ini,
        BUKAN nyusun teks sendiri-sendiri, biar konsisten dan gampang
        diubah lagi nanti kalau kamu mau ganti nama mesinnya jadi lebih
        spesifik (mis. "Motor Pompa Utama") -- tinggal ubah field "label"
        di SLOT_DEFS (config.py), otomatis kepakai di semua tempat ini."""
        if idx is None or idx < 0 or idx >= len(SLOT_DEFS):
            return "- Pilih Slot -"
        slot = SLOT_DEFS[idx]
        if getattr(self, "ui_mode", "awam") == "teknisi":
            return f"{slot['label']} ({slot['cluster_label']})"
        return slot["label"]


    def _menu_style(self, active):
        if active:
            return f"background-color: {COL_ACCENT}; color: #000000; font-size: 11px; font-weight: bold; border: 1px solid white; border-radius: 4px;"
        return f"background-color: #cfcfcf; color: #000000; font-size: 11px; font-weight: bold; border: 1px solid #444; border-radius: 4px;"


    def _change_page(self, idx):
        self.stack.setCurrentIndex(idx)
        # Halaman Log Detail (5) dibuka dari daftar rekaman, bukan dari nav
        # bawah -- kalau lagi di situ, sorot tombol yang menuju halaman
        # LOGS SAVES (page 1) supaya tetap ada tombol yang nyala.
        target_for_highlight = 1 if idx == 5 else idx
        highlight_pos = next(
            (pos for pos, (_, target) in enumerate(self.nav_map) if target == target_for_highlight),
            -1
        )
        for i, btn in enumerate(self.menu_buttons):
            btn.setStyleSheet(self._menu_style(i == highlight_pos))


    def _apply_ui_mode(self):
        """Terapkan self.ui_mode ke UI: ganti label 4 tombol nav bawah,
        sembunyikan tombol perintah teknis (kalibrasi/reboot/dst) di mode
        Awam supaya pemilik UMKM gak sengaja pencet perintah yang gak dia
        ngerti, lalu buka halaman pertama dari mode itu."""
        self.nav_map = self.nav_map_teknisi if self.ui_mode == "teknisi" else self.nav_map_awam
        for i, (label, _target) in enumerate(self.nav_map):
            self.menu_buttons[i].setText(label)

        is_teknisi = (self.ui_mode == "teknisi")
        for w in (self.btn_cek1m, self.btn_slot_res, self.btn_del_base,
                  self.btn_recal, self.btn_reboot_esp, self.btn_debug):
            w.setVisible(is_teknisi)

        self.btn_mode_toggle.setText("MODE: TEKNISI" if is_teknisi else "MODE: AWAM")
        self.lbl_machine_active.setText(f"⚙️  {self._slot_display_name(self.selected_slot_idx)}")
        self._change_page(self.nav_map[0][1])


    def _toggle_ui_mode(self):
        self.ui_mode = "teknisi" if self.ui_mode == "awam" else "awam"
        self._apply_ui_mode()


    def _confirm_reboot_esp(self):
        reply = QMessageBox.question(
            self, "Konfirmasi Reboot",
            "Reboot ESP32 sekarang?\n\nKoneksi akan terputus ~2-3 detik.",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No
        )
        if reply == QMessageBox.Yes:
            self._send_command('X')
            

    def _apply_header_alarm_state(self, status_key):
        color = {"bahaya": COL_BAD, "waspada": COL_WARN, "diam": COL_IDLE}.get(status_key, COL_ACCENT)
        if self.packet_loss_flag:
            color = COL_WARN
        self.header_frame.setStyleSheet(
            f"background-color: {COL_HEADER_BG}; border-radius: 4px; "
            f"border-bottom: 2px solid {color};"
        )


    def _evaluate_diagnosis(self, v, temp, device_status=""):
        status_key = (device_status or "").strip().lower()
        self._apply_header_alarm_state(status_key)
        
        status_map = {
            "diam": ("STATUS: DIAM / MOTOR MATI", COL_IDLE, "Mesin tidak beroperasi (Standby / Off).", "● MOTOR DIAM"),
            "bahaya": ("STATUS: BAHAYA (CRITICAL)", COL_BAD, "Terjadi anomali gesekan parah!", "● DEVIASI BAHAYA"),
            "waspada": ("STATUS: WASPADA (WARNING)", COL_WARN, "Indikasi awal degradasi mekanis.", "● STATUS WASPADA"),
            "normal": ("STATUS: NORMAL", COL_OK, "Seluruh parameter berjalan normal.", "● SYSTEM ONLINE"),
            "warming": ("STATUS: MENYIAPKAN SENSOR", "#888888", "Mengambil sample pertama.", "● WARMING UP"),
            "calibrating": ("STATUS: SEDANG KALIBRASI", "#888888", "Alat sedang merekam baseline mesin (180 detik).", "● KALIBRASI BASELINE"),
            "notcalibrated": ("STATUS: BELUM KALIBRASI", "#888888", "Belum kalibrasi baseline.", "● KALIBRASI BASELINE"),
            "sensorfault": ("STATUS: SENSOR ERROR", COL_WARN, "Data sensor basi.", "● SENSOR FAULT"),
        }
        
        if status_key in status_map:
            title, color, desc, sys_txt = status_map[status_key]
            if self.packet_loss_flag:
                sys_txt = "● EMI / PACKET LOSS WARNING"
                color = COL_WARN
            self.lbl_sys_status.setText(sys_txt)
            self.lbl_sys_status.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {color};")
            self.lbl_diag_desc_summary.setText(f"{title} - {desc}")
            return

        if v > 0.25 or temp > 50.0:
            status_txt, status_col = "● DEVIASI BAHAYA", COL_BAD
            desc = "STATUS: BAHAYA - Matikan mesin!"
        elif v > 0.18 or temp > 42.0:
            status_txt, status_col = "● STATUS WASPADA", COL_WARN
            desc = "STATUS: WASPADA - Indikasi awal degradasi."
        else:
            status_txt, status_col = "● SYSTEM ONLINE", COL_OK
            desc = "STATUS: NORMAL."

        if self.packet_loss_flag:
            status_txt = "● EMI / PACKET LOSS WARNING"
            status_col = COL_WARN

        self.lbl_sys_status.setText(status_txt)
        self.lbl_sys_status.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {status_col};")
        self.lbl_diag_desc_summary.setText(desc)


    def _reset_session(self):
        self.session_sample_count = 0
        self.session_rpm_sum = 0.0
        self.session_d2_max = 0.0
        self.session_worst_status = "Normal"
        self.session_waspada_count = 0
        self.session_bahaya_count = 0
        self.session_diam_count = 0
        self.anomaly_events = []
        self.last_processed_tick = self.tick
        self.list_anomali.clear()
        self.list_anomali.addItem("Tidak ada kejadian anomali sepanjang sesi ini.")
        self.list_riwayat.clear()
        self.list_riwayat.addItem("Belum ada kejadian pada sesi ini.")
        self._render_session_summary()


    def _show_debug_info(self):
        port_info = self.ser.port if (self.ser is not None) else "(belum ada koneksi)"
        slot_txt = (SLOT_DEFS[self.selected_slot_idx]["label"] + " / " + SLOT_DEFS[self.selected_slot_idx]["cluster_label"]) \
            if self.selected_slot_idx >= 0 else "Belum dipilih"
        info = (
            f"Status koneksi   : {'TERSAMBUNG' if self.serial_connected else 'TIDAK TERSAMBUNG'}\n"
            f"Port serial      : {port_info}\n"
            f"Baud rate        : {BAUD_RATE}\n"
            f"Packet Loss Flag : {'TERDETEKSI (EMI Noise)' if self.packet_loss_flag else 'Aman'}\n"
            f"Slot Aktif       : {slot_txt}\n"
            f"Baris JSON akhir : {self.last_raw_line or '(belum ada data masuk)'}\n"
            f"Vib/Snd/Tmp      : {self.current_v}, {self.current_a}, {self.current_temp}\n"
            f"RPM / D²         : {self.current_rpm}, {self.current_d2}\n"
            f"Status firmware  : {self.current_status_device or '-'}"
        )
        QMessageBox.information(self, "DEBUG - Info Koneksi & Data Terakhir", info)


    def _render_session_summary(self):
        self.lbl_session_summary.setText(
            f"Sesi: {self.session_sample_count} sample | "
            f"RPM rata-rata: {(self.session_rpm_sum / self.session_sample_count) if self.session_sample_count else 0.0:.1f} | "
            f"D² max: {self.session_d2_max:.2f} | Kondisi terparah: {self.session_worst_status} | "
            f"Waspada: {self.session_waspada_count}x, Bahaya: {self.session_bahaya_count}x."
        )
        # Kotak statistik sederhana di tab Riwayat (mode Awam) -- angka sama
        # persis, cuma gak pakai istilah teknis (RPM/D2).
        self.lbl_riwayat_waspada.setText(str(self.session_waspada_count))
        self.lbl_riwayat_bahaya.setText(str(self.session_bahaya_count))
        self.lbl_riwayat_sample.setText(str(self.session_sample_count))


    def _update_gui(self):
        # Update Jam & Tanggal Real-Time di Header
        now_dt = datetime.now()
        self.time_lbl.setText(now_dt.strftime("%H:%M:%S"))
        self.date_lbl.setText(now_dt.strftime("%d/%m/%Y"))

        self.lbl_conn_dot.setStyleSheet(
            f"font-size: 10px; font-weight: bold; color: {COL_OK if (self.serial_connected and not self.packet_loss_flag) else COL_WARN};"
        )

        if self.current_v is not None:
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

            # Panel ringkasan diagnosa di tab SUMMARY (lihat _page_summary)
            self.lbl_hs_val_sum.setText(f"{self.current_health_score:.1f}%")
            self.lbl_diag_val_sum.setText(f"{self.current_diag_label} ({self.current_diag_conf*100:.0f}%)")
            self.lbl_servis_val_sum.setText(f"{self.current_servis}")

            self.lbl_val_v.setText(f"{self.current_v:.2f} G")

            if self.current_vx is not None:
                self.lbl_val_vxyz.setText(
                    f"(X: {self.current_vx:.2f} | Y: {self.current_vy:.2f} | Z: {self.current_vz:.2f} G)"
                )
            self.lbl_val_a.setText(f"{self.current_a:.1f} dB")
            self.lbl_val_temp.setText(f"{self.current_temp:.1f} °C")
            if self.current_rpm is not None:
                self.lbl_val_rpm.setText(f"{self.current_rpm:.0f}")
            if self.current_d2 is not None:
                self.lbl_val_d2.setText(f"{self.current_d2:.2f}")

            rpm_txt = f"{self.current_rpm:.0f}" if self.current_rpm is not None else "--"
            d2_txt = f"{self.current_d2:.2f}" if self.current_d2 is not None else "--"
            self.lbl_proc_snapshot.setText(
                f"Live | Vib: {self.current_v:.2f} G | Snd: {self.current_a:.1f} dB | "
                f"Tmp: {self.current_temp:.1f} °C | RPM: {rpm_txt} | D²: {d2_txt}"
            )

            status_key = (self.current_status_device or "").strip().lower()
            self.gauge_v.set_value(self.current_v, status_key)
            self.gauge_s.set_value(self.current_a, status_key)
            self.gauge_t.set_value(self.current_temp, status_key)

            if self.selected_slot_idx >= 0:
                slot_def = SLOT_DEFS[self.selected_slot_idx]
                machine_name = f"{slot_def['label']} ({slot_def['cluster_label']})"
            else:
                machine_name = "Belum dipilih"
            self.lbl_sum_machine.setText(f"Target: {machine_name}")

            self._evaluate_diagnosis(self.current_v, self.current_temp, self.current_status_device)

            # CATATAN: halaman Beranda & Rekomendasi (mode Awam) SENGAJA TIDAK
            # di-update di sini. Dulu di titik ini kode langsung nulis ke
            # lbl_beranda_* pakai data live yang baru masuk -- akibatnya
            # Beranda kelihatan "real-time" padahal user belum pernah pencet
            # Check sama sekali. Sekarang Beranda cuma di-render lewat
            # _render_beranda(), dan itu CUMA dipanggil dari _finish_check()
            # di page_flow.py -- jadi hasil di Beranda selalu berasal dari
            # sesi Check yang BENERAN sudah selesai, bukan aliran data live.

            # self.check_active cuma True selama jendela 60 detik sesi Check
            # (lihat page_flow.py) -- ini yang bikin "Total Data Masuk" & co.
            # BERHENTI nambah begitu sesi selesai, bukan numpuk terus-terusan
            # kayak sebelumnya.
            if self.check_active and self.tick != self.last_processed_tick:
                self.last_processed_tick = self.tick
                self.session_sample_count += 1
                if self.current_rpm is not None:
                    self.session_rpm_sum += self.current_rpm
                if self.current_d2 is not None:
                    self.session_d2_max = max(self.session_d2_max, self.current_d2)

                if status_key == "waspada":
                    self.session_waspada_count += 1
                elif status_key == "bahaya":
                    self.session_bahaya_count += 1
                elif status_key == "diam":
                    # Dipakai _finish_check (page_flow.py) buat ngecek "apa
                    # SELURUH sesi ini mesinnya diam total" -- kalau iya,
                    # hasil Check harus jujur bilang "Diam", bukan ke-anggap
                    # "Normal" (lihat catatan panjang di _finish_check soal
                    # kenapa severity "diam" gak bisa dipakai buat ini).
                    self.session_diam_count += 1

                if status_key in STATUS_SEVERITY:
                    if STATUS_SEVERITY[status_key] > STATUS_SEVERITY.get(self.session_worst_status.lower(), 0):
                        self.session_worst_status = self.current_status_device.capitalize()

                if status_key in ("waspada", "bahaya"):
                    ts = datetime.now().strftime("%H:%M:%S")
                    event_txt = (
                        f"[{ts}] {self.current_status_device.upper()} — RPM {rpm_txt}, D² {d2_txt}, "
                        f"Vib {self.current_v:.2f}G, Tmp {self.current_temp:.1f}°C"
                    )
                    self.anomaly_events.append(event_txt)
                    if self.list_anomali.count() == 1 and self.list_anomali.item(0).text().startswith("Tidak ada"):
                        self.list_anomali.clear()
                    self.list_anomali.addItem(event_txt)
                    self.list_anomali.scrollToBottom()

                    # Versi bahasa awam dari kejadian yang sama, buat tab Riwayat.
                    event_txt_awam = f"[{ts}] Kondisi {self.current_status_device.upper()} terdeteksi"
                    if self.list_riwayat.count() == 1 and self.list_riwayat.item(0).text().startswith("Belum ada"):
                        self.list_riwayat.clear()
                    self.list_riwayat.addItem(event_txt_awam)
                    self.list_riwayat.scrollToBottom()

                self._render_session_summary()
        elif not self.serial_connected:
            self.lbl_sys_status.setText("● MENCARI PERANGKAT (SERIAL)...")
            self.lbl_sys_status.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {COL_WARN};")
        else:
            self.lbl_sys_status.setText("● TERSAMBUNG — MENUNGGU DATA JSON...")
            self.lbl_sys_status.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {COL_ACCENT};")
            self.lbl_proc_snapshot.setText("Menunggu koneksi serial ke ESP32...")

