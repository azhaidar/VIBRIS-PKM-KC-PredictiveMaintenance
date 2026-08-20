from PyQt5.QtWidgets import (
    QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush
import pyqtgraph as pg
from datetime import datetime
import json

from config import (
    COL_PANEL_DARK, COL_TEXT_LIGHT, COL_TEXT_DIM, COL_ACCENT, COL_ACCENT_DIM,
    COL_OK, COL_WARN, COL_BAD, SLOT_DEFS, SLOT_STATE_FILE, LAST_CHECK_FILE,
    CALIBRATION_DURATION_S, CHECK_DURATION_S, SLOT_SELECT_SETTLE_S,
    STATUS_CALIBRATING,
)

# Index halaman di self.stack (lihat urutan stack.addWidget di
# dashboard_core.py __init__ -- kalau urutan itu diubah, angka di sini
# HARUS ikut diubah).
PAGE_MACHINE_SELECT = 4
PAGE_BERANDA = 6
PAGE_RIWAYAT = 7
PAGE_LOADING = 9
PAGE_CALIBRATING = 10
PAGE_CHECKING = 11
PAGE_HASIL_KALIBRASI = 12
PAGE_HASIL_CHECK = 13
PAGE_PILIH_AKSI = 14


class FlowMixin:
    """Alur tuntunan otomatis: pilih slot -> loading 5 detik -> cabang ke
    Kalibrasi (180 detik) kalau slotnya masih kosong, atau Check (60 detik)
    kalau slotnya udah punya baseline -> halaman hasil.

    Ini beda dari mixin halaman lain (page_raw.py dkk) karena isinya bukan
    cuma "cara gambar 1 halaman statis" -- mixin ini juga pegang LOGIKA
    alurnya (timer countdown, kapan pindah halaman, kapan kirim command apa
    ke ESP32). Sengaja digabung jadi satu supaya alurnya gampang dibaca
    urut dari atas ke bawah, gak kececer di banyak file.

    CATATAN PENTING buat siapa pun yang lanjutin kode ini: durasi kalibrasi
    (180 detik) dan check (60 detik) itu HARUS sama persis dengan firmware
    (lihat CALIBRATION_DURATION_S / CHECK_DURATION_S di config.py, sumbernya
    main.ino dan CheckSession.h di repo GitHub). Countdown di sini cuma buat
    tampilan; keputusan "beneran selesai apa belum" pas Kalibrasi tetap
    nunggu status firmware bukan "Calibrating" lagi (lihat _calibration_tick)
    -- BUKAN nunggu angka 0 doang, biar gak salah declare selesai kalau
    ESP-nya sedikit lebih lambat dari perkiraan.

    Untuk Check, firmware TIDAK otomatis kirim hasil akhir begitu 60 detik
    habis -- harus diminta manual lewat command 'P' (lihat _finish_check).
    Field JSON hasil `CheckSessionSummary` (dominant_status, count_normal,
    dst.) belum saya konfirmasi nama persisnya di JSON asli (yang saya
    lihat baru nama field di struct C++-nya) -- kalau ternyata beda,
    _finish_check tinggal disesuaikan, gak perlu bongkar alur yang lain."""

    # ---------------- Halaman ----------------

    def _page_pilih_aksi(self):
        """Halaman BARU: setelah pilih slot, user harus PENCET SENDIRI mau
        Kalibrasi atau Check -- gak ada lagi tebak-tebakan otomatis.

        Ini ganti total cara kerja yang lama, yang langsung nebak sendiri
        (baca status ESP32, terus "ujug-ujug" lompat ke Check atau
        Kalibrasi tanpa nanya). User protes itu karena kelihatan kayak
        alatnya jalan sendiri tanpa dikasih tau, dan susah dibedain "ini
        beneran mau Check apa mau Kalibrasi". Sekarang user yang mutusin,
        alat cuma ngasih tau kondisi (badge kalibrasi) buat bantu mutusin,
        bukan mutusin sendiri.

        Tombol Check SENGAJA dikunci (disabled) kalau slotnya belum
        terkalibrasi -- ini yang menjaga aturan "Check tanpa Kalibrasi gak
        boleh" tetap berlaku, walau sekarang usernya yang pencet manual.
        Tombol Kalibrasi TETAP selalu aktif -- re-kalibrasi/timpa baseline
        lama harus tetap bisa dilakukan kapan saja."""
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(16, 20, 16, 20)
        layout.setSpacing(10)
        layout.addStretch(1)

        self.lbl_aksi_slot = QLabel("Mesin: --")
        self.lbl_aksi_slot.setAlignment(Qt.AlignCenter)
        self.lbl_aksi_slot.setWordWrap(True)
        self.lbl_aksi_slot.setStyleSheet(f"font-size: 14px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(self.lbl_aksi_slot)

        self.lbl_aksi_badge = QLabel("○ Kosong")
        self.lbl_aksi_badge.setAlignment(Qt.AlignCenter)
        self.lbl_aksi_badge.setStyleSheet(f"font-size: 10px; font-weight: bold; color: {COL_TEXT_DIM};")
        layout.addWidget(self.lbl_aksi_badge)

        layout.addSpacing(10)

        self.btn_aksi_kalibrasi = QPushButton("🔧  Mulai Kalibrasi")
        self.btn_aksi_kalibrasi.setStyleSheet(
            f"background-color: {COL_WARN}; color: #000000; font-weight: bold; "
            f"font-size: 11px; height: 42px; border-radius: 6px;"
        )
        self.btn_aksi_kalibrasi.clicked.connect(self._user_start_kalibrasi)
        layout.addWidget(self.btn_aksi_kalibrasi)

        self.lbl_aksi_kalibrasi_hint = QLabel("Rekam ulang kondisi normal mesin sebagai patokan baru (180 detik).")
        self.lbl_aksi_kalibrasi_hint.setAlignment(Qt.AlignCenter)
        self.lbl_aksi_kalibrasi_hint.setWordWrap(True)
        self.lbl_aksi_kalibrasi_hint.setStyleSheet(f"font-size: 8px; color: {COL_TEXT_DIM};")
        layout.addWidget(self.lbl_aksi_kalibrasi_hint)

        layout.addSpacing(6)

        self.btn_aksi_check = QPushButton("🔍  Mulai Check")
        self.btn_aksi_check.setStyleSheet(
            f"background-color: {COL_ACCENT}; color: #000000; font-weight: bold; "
            f"font-size: 11px; height: 42px; border-radius: 6px;"
        )
        self.btn_aksi_check.clicked.connect(self._user_start_check)
        # Default AMAN pas halaman ini pertama kali dibuat: terkunci. Baru
        # dibuka kalau _refresh_pilih_aksi_page() beneran ngecek slot_calibrated
        # dan ketemu True -- jangan sampai ada celah waktu tombol ini aktif
        # padahal belum ada slot yang dipilih / belum dicek statusnya.
        self.btn_aksi_check.setEnabled(False)
        layout.addWidget(self.btn_aksi_check)

        self.lbl_aksi_check_hint = QLabel("Bandingkan kondisi mesin sekarang dengan baseline (60 detik).")
        self.lbl_aksi_check_hint.setAlignment(Qt.AlignCenter)
        self.lbl_aksi_check_hint.setWordWrap(True)
        self.lbl_aksi_check_hint.setStyleSheet(f"font-size: 8px; color: {COL_TEXT_DIM};")
        layout.addWidget(self.lbl_aksi_check_hint)

        layout.addStretch(1)

        # DULU tombol hapus baseline (BASE(Z) di header) CUMA ada di mode
        # Teknisi -- user mode Awam gak punya cara sama sekali buat hapus
        # baseline sendiri dari layar ini. Sengaja ditaruh di SINI (bukan
        # tombol besar berwarna kayak Kalibrasi/Check) supaya keliatan beda
        # tingkat bahayanya -- ini tindakan MERUSAK DATA PERMANEN, harus
        # tetap ada, tapi jangan gampang ke-pencet gak sengaja.
        self.btn_aksi_hapus = QPushButton("🗑  Hapus Baseline Slot Ini")
        self.btn_aksi_hapus.setStyleSheet(
            f"background-color: transparent; color: {COL_BAD}; font-size: 9px; "
            f"height: 26px; border: 1px solid {COL_BAD}; border-radius: 4px;"
        )
        self.btn_aksi_hapus.clicked.connect(self._confirm_delete_baseline)
        self.btn_aksi_hapus.setEnabled(False)  # default aman, dibuka _refresh_pilih_aksi_page kalau ada baseline
        layout.addWidget(self.btn_aksi_hapus)

        btn_kembali = QPushButton("‹ Kembali ke Pilih Slot")
        btn_kembali.setStyleSheet(
            f"background-color: {COL_PANEL_DARK}; color: {COL_TEXT_LIGHT}; font-size: 9px; "
            f"height: 28px; border: 1px solid {COL_TEXT_DIM}; border-radius: 4px;"
        )
        btn_kembali.clicked.connect(lambda: self._change_page(PAGE_MACHINE_SELECT))
        layout.addWidget(btn_kembali)

        return page

    def _refresh_pilih_aksi_page(self):
        """Update teks+badge+status tombol di halaman Pilih Aksi, dipanggil
        tiap kali self.slot_calibrated berubah ATAU tiap kali halaman ini
        mau ditampilkan (dari _on_slot_selected)."""
        if self.selected_slot_idx < 0:
            return
        is_calibrated = self.slot_calibrated.get(self.selected_slot_idx, False)
        self.lbl_aksi_slot.setText(f"Mesin: {self._slot_display_name(self.selected_slot_idx)}")
        if is_calibrated:
            self.lbl_aksi_badge.setText("✓ Sudah Terkalibrasi")
            self.lbl_aksi_badge.setStyleSheet(f"font-size: 10px; font-weight: bold; color: {COL_OK};")
        else:
            self.lbl_aksi_badge.setText("○ Kosong -- belum ada baseline")
            self.lbl_aksi_badge.setStyleSheet(f"font-size: 10px; font-weight: bold; color: {COL_TEXT_DIM};")

        # INI yang menegakkan aturan "Check tanpa Kalibrasi gak boleh":
        # tombol Check dikunci total (gak bisa dipencet) kalau baseline-nya
        # belum ada. Tombol Kalibrasi TETAP selalu bisa dipencet -- baik
        # buat kalibrasi pertama kali maupun buat menimpa/re-kalibrasi
        # baseline yang udah ada.
        self.btn_aksi_check.setEnabled(is_calibrated)
        if is_calibrated:
            self.lbl_aksi_check_hint.setText("Bandingkan kondisi mesin sekarang dengan baseline (60 detik).")
        else:
            self.lbl_aksi_check_hint.setText("Terkunci -- jalankan Kalibrasi dulu, baseline-nya belum ada.")

        # Gak ada gunanya nampilin "Hapus Baseline" aktif kalau baseline-nya
        # emang belum ada -- gak ada apa-apa buat dihapus.
        self.btn_aksi_hapus.setEnabled(is_calibrated)

    def _user_start_kalibrasi(self):
        self._pending_action = "calibrate"
        self._begin_slot_flow()

    def _user_start_check(self):
        if not self.slot_calibrated.get(self.selected_slot_idx, False):
            # Jaring pengaman kedua -- seharusnya gak kepanggil karena
            # tombolnya udah di-disable di _refresh_pilih_aksi_page, tapi
            # tetap dicek ulang di sini biar gak ada jalan pintas.
            QMessageBox.warning(self, "Belum Ada Baseline", "Jalankan Kalibrasi dulu sebelum Check.")
            return
        self._pending_action = "check"
        self._begin_slot_flow()

    def _page_loading(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(20, 30, 20, 30)
        layout.setSpacing(10)
        layout.addStretch(1)

        self.lbl_loading_text = QLabel("Menyiapkan...")
        self.lbl_loading_text.setAlignment(Qt.AlignCenter)
        self.lbl_loading_text.setWordWrap(True)
        self.lbl_loading_text.setStyleSheet(f"font-size: 13px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(self.lbl_loading_text)

        self.lbl_loading_countdown = QLabel(str(SLOT_SELECT_SETTLE_S))
        self.lbl_loading_countdown.setAlignment(Qt.AlignCenter)
        self.lbl_loading_countdown.setStyleSheet(f"font-size: 34px; font-weight: bold; color: {COL_ACCENT};")
        layout.addWidget(self.lbl_loading_countdown)

        layout.addStretch(1)

        btn_bg_loading = QPushButton("Lihat Nanti (jalan di latar belakang)")
        btn_bg_loading.setStyleSheet(
            f"background-color: {COL_PANEL_DARK}; color: {COL_TEXT_LIGHT}; font-size: 8px; "
            f"height: 26px; border: 1px solid {COL_TEXT_DIM}; border-radius: 4px;"
        )
        btn_bg_loading.clicked.connect(lambda: self._go_to_background(PAGE_MACHINE_SELECT))
        layout.addWidget(btn_bg_loading)
        return page

    def _page_calibrating(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(20, 25, 20, 25)
        layout.setSpacing(8)
        layout.addStretch(1)

        lbl_title = QLabel("⏳ SEDANG KALIBRASI")
        lbl_title.setAlignment(Qt.AlignCenter)
        lbl_title.setStyleSheet(f"font-size: 14px; font-weight: bold; color: {COL_WARN};")
        layout.addWidget(lbl_title)

        self.lbl_calib_slot = QLabel("Slot: --")
        self.lbl_calib_slot.setAlignment(Qt.AlignCenter)
        self.lbl_calib_slot.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_DIM};")
        layout.addWidget(self.lbl_calib_slot)

        self.lbl_calib_countdown = QLabel("3:00")
        self.lbl_calib_countdown.setAlignment(Qt.AlignCenter)
        self.lbl_calib_countdown.setStyleSheet(f"font-size: 44px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(self.lbl_calib_countdown)

        lbl_desc = QLabel(
            "Jangan matikan atau pindahkan mesin dulu. Alat sedang merekam "
            "kondisi normal mesin sebagai patokan (baseline)."
        )
        lbl_desc.setAlignment(Qt.AlignCenter)
        lbl_desc.setWordWrap(True)
        lbl_desc.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_DIM};")
        layout.addWidget(lbl_desc)

        layout.addStretch(1)

        btn_bg_calib = QPushButton("Lihat Nanti (tetap jalan di latar belakang)")
        btn_bg_calib.setStyleSheet(
            f"background-color: {COL_PANEL_DARK}; color: {COL_TEXT_LIGHT}; font-size: 8px; "
            f"height: 26px; border: 1px solid {COL_TEXT_DIM}; border-radius: 4px;"
        )
        btn_bg_calib.clicked.connect(lambda: self._go_to_background(PAGE_BERANDA))
        layout.addWidget(btn_bg_calib)
        return page

    def _page_checking(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(20, 25, 20, 25)
        layout.setSpacing(8)
        layout.addStretch(1)

        lbl_title = QLabel("🔍 SEDANG CHECK")
        lbl_title.setAlignment(Qt.AlignCenter)
        lbl_title.setStyleSheet(f"font-size: 14px; font-weight: bold; color: {COL_ACCENT};")
        layout.addWidget(lbl_title)

        self.lbl_check_slot = QLabel("Slot: --")
        self.lbl_check_slot.setAlignment(Qt.AlignCenter)
        self.lbl_check_slot.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_DIM};")
        layout.addWidget(self.lbl_check_slot)

        self.lbl_check_countdown = QLabel("1:00")
        self.lbl_check_countdown.setAlignment(Qt.AlignCenter)
        self.lbl_check_countdown.setStyleSheet(f"font-size: 44px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(self.lbl_check_countdown)

        lbl_desc = QLabel("Alat sedang membandingkan kondisi mesin sekarang dengan data normalnya.")
        lbl_desc.setAlignment(Qt.AlignCenter)
        lbl_desc.setWordWrap(True)
        lbl_desc.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_DIM};")
        layout.addWidget(lbl_desc)

        layout.addStretch(1)

        btn_bg_check = QPushButton("Lihat Nanti (tetap jalan di latar belakang)")
        btn_bg_check.setStyleSheet(
            f"background-color: {COL_PANEL_DARK}; color: {COL_TEXT_LIGHT}; font-size: 8px; "
            f"height: 26px; border: 1px solid {COL_TEXT_DIM}; border-radius: 4px;"
        )
        btn_bg_check.clicked.connect(lambda: self._go_to_background(PAGE_BERANDA))
        layout.addWidget(btn_bg_check)
        return page

    def _page_hasil_kalibrasi(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(20, 25, 20, 25)
        layout.setSpacing(10)
        layout.addStretch(1)

        lbl_icon = QLabel("✅")
        lbl_icon.setAlignment(Qt.AlignCenter)
        lbl_icon.setStyleSheet("font-size: 42px;")
        layout.addWidget(lbl_icon)

        self.lbl_hasil_kalib_text = QLabel("Kalibrasi selesai!")
        self.lbl_hasil_kalib_text.setAlignment(Qt.AlignCenter)
        self.lbl_hasil_kalib_text.setWordWrap(True)
        self.lbl_hasil_kalib_text.setStyleSheet(f"font-size: 11px; font-weight: bold; color: {COL_OK};")
        layout.addWidget(self.lbl_hasil_kalib_text)

        layout.addStretch(1)

        btn_lanjut = QPushButton("Lanjut ke Beranda")
        btn_lanjut.setStyleSheet(
            f"background-color: {COL_ACCENT}; color: #000000; font-weight: bold; "
            f"font-size: 10px; height: 32px; border-radius: 4px;"
        )
        btn_lanjut.clicked.connect(lambda: self._change_page(PAGE_BERANDA))
        layout.addWidget(btn_lanjut)

        return page

    def _page_hasil_check(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(20, 20, 20, 20)
        layout.setSpacing(8)
        layout.addStretch(1)

        self.lbl_hasil_check_icon = QLabel("✅")
        self.lbl_hasil_check_icon.setAlignment(Qt.AlignCenter)
        self.lbl_hasil_check_icon.setStyleSheet("font-size: 38px;")
        layout.addWidget(self.lbl_hasil_check_icon)

        self.lbl_hasil_check_status = QLabel("Hasil Check: --")
        self.lbl_hasil_check_status.setAlignment(Qt.AlignCenter)
        self.lbl_hasil_check_status.setStyleSheet(f"font-size: 13px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(self.lbl_hasil_check_status)

        self.lbl_hasil_check_detail = QLabel("")
        self.lbl_hasil_check_detail.setAlignment(Qt.AlignCenter)
        self.lbl_hasil_check_detail.setWordWrap(True)
        self.lbl_hasil_check_detail.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_DIM};")
        layout.addWidget(self.lbl_hasil_check_detail)

        layout.addStretch(1)

        btn_row = QHBoxLayout()
        btn_riwayat = QPushButton("Lihat Riwayat")
        btn_riwayat.setStyleSheet(
            f"background-color: {COL_PANEL_DARK}; color: {COL_TEXT_LIGHT}; font-weight: bold; "
            f"font-size: 9px; height: 30px; border: 1px solid {COL_ACCENT}; border-radius: 4px;"
        )
        btn_riwayat.clicked.connect(lambda: self._change_page(PAGE_RIWAYAT))

        btn_lanjut = QPushButton("Kembali ke Beranda")
        btn_lanjut.setStyleSheet(
            f"background-color: {COL_ACCENT}; color: #000000; font-weight: bold; "
            f"font-size: 9px; height: 30px; border-radius: 4px;"
        )
        btn_lanjut.clicked.connect(lambda: self._change_page(PAGE_BERANDA))

        btn_row.addWidget(btn_riwayat)
        btn_row.addWidget(btn_lanjut)
        layout.addLayout(btn_row)

        return page

    # ---------------- Kunci navigasi selama proses berjalan ----------------

    def _set_nav_locked(self, locked):
        # PENTING -- INI BERUBAH TOTAL dari versi sebelumnya. Dulu fungsi ini
        # mengunci SEMUA navigasi (4 tombol menu bawah + header) selama
        # Loading/Kalibrasi/Check jalan, jadi user TERPAKSA menatap layar
        # countdown, gak bisa buka tab lain. Itu gak masuk akal: proses
        # Kalibrasi/Check jalan SENDIRI di ESP32 (digerbang waktu di firmware,
        # lihat _kembali_ke_latar_belakang di bawah) -- dashboard cuma
        # NONTON, jadi gak ada alasan teknis buat mengunci layar user.
        #
        # Sekarang yang dikunci CUMA tombol command teknis (SESI(K), SLOT(P),
        # BASE(Z), KALIBRASI, REBOOT, DEBUG) -- itu tetap perlu dikunci
        # karena kalau dipencet di tengah proses, bisa kirim command yang
        # bentrok/menimpa proses yang lagi jalan di firmware. Tombol menu
        # bawah dan header (lbl_machine_active) TETAP AKTIF -- user bebas
        # pindah tab sambil Kalibrasi/Check tetap jalan di latar belakang.
        for w in (self.btn_cek1m, self.btn_slot_res, self.btn_del_base,
                  self.btn_recal, self.btn_reboot_esp, self.btn_debug):
            w.setEnabled(not locked)

    def _stop_flow_timer(self):
        if self.flow_timer is not None:
            self.flow_timer.stop()
            self.flow_timer = None

    # ---------------- Catatan lokal: slot mana yang udah dikalibrasi ----------------

    def _load_slot_calib_state(self):
        """Dipanggil sekali di __init__. Isi self.slot_calibrated dari file
        JSON lokal (lihat SLOT_STATE_FILE di config.py). Ini CUMA buat
        nunjukin badge di kartu pilih slot -- keputusan alur yang
        sebenarnya (Kalibrasi vs Check) tetap dari status asli ESP32
        (_proceed_pending_action), bukan dari catatan ini."""
        self.slot_calibrated = {i: False for i in range(len(SLOT_DEFS))}
        try:
            with open(SLOT_STATE_FILE, 'r') as f:
                saved = json.load(f)
            for k, v in saved.items():
                self.slot_calibrated[int(k)] = bool(v)
        except (FileNotFoundError, json.JSONDecodeError, ValueError, OSError):
            pass

    def _save_slot_calib_state(self):
        try:
            with open(SLOT_STATE_FILE, 'w') as f:
                json.dump(self.slot_calibrated, f)
        except OSError as e:
            print(f"[SLOT STATE] Gagal simpan catatan kalibrasi: {e}")

    # ---------------- Catatan lokal: hasil Check terakhir per slot ----------------

    def _load_last_check_results(self):
        """Dipanggil sekali di __init__ (dashboard_core.py). Isi
        self.last_check_results dari file JSON lokal (LAST_CHECK_FILE di
        config.py) supaya hasil Check terakhir tiap slot masih kelihatan
        walau aplikasi dashboard ini baru aja dibuka ulang -- BUKAN
        ke-reset jadi 'belum ada hasil' tiap kali restart seperti
        sebelumnya (dulu cuma disimpan di RAM/self.last_check_result,
        hilang begitu proses Python-nya berhenti)."""
        self.last_check_results = {}
        try:
            with open(LAST_CHECK_FILE, 'r') as f:
                saved = json.load(f)
            for k, v in saved.items():
                self.last_check_results[int(k)] = v
        except (FileNotFoundError, json.JSONDecodeError, ValueError, OSError):
            pass

    def _save_last_check_results(self):
        try:
            with open(LAST_CHECK_FILE, 'w') as f:
                json.dump({str(k): v for k, v in self.last_check_results.items()}, f)
        except OSError as e:
            print(f"[LAST CHECK] Gagal simpan hasil Check terakhir: {e}")

    def _on_machine_active_clicked(self):
        """Klik header (nama mesin di pojok kiri atas). Kalau ada proses
        yang lagi jalan di latar belakang, lompat ke situ. Kalau gak ada,
        buka halaman pilih slot -- perilaku lama."""
        if self._active_flow_page is not None:
            self._change_page(self._active_flow_page)
        else:
            self._change_page(PAGE_MACHINE_SELECT)

    def _restore_machine_active_label(self):
        self.lbl_machine_active.setText(f"⚙️  {self._slot_display_name(self.selected_slot_idx)}")

    def _go_to_background(self, target_page):
        """Dipanggil dari tombol "Lihat Nanti" di halaman Loading/Kalibrasi/
        Check. INI BUKAN "batal" -- ESP32 gak punya perintah buat
        menghentikan kalibrasi/check yang lagi jalan (sudah saya cek ke
        firmware, gak ada). Jadi tombol ini CUMA pindah layar, timer & proses
        di ESP32 tetap jalan terus di baliknya. Begitu waktunya selesai,
        hasilnya tetap otomatis muncul di Beranda / halaman hasil, walau
        user lagi buka tab lain."""
        stage = self._active_flow_page
        if stage == PAGE_CALIBRATING:
            QMessageBox.information(
                self, "Tetap Berjalan di Latar Belakang",
                "Kalibrasi TETAP lanjut di alat sampai 180 detik selesai, "
                "walau kamu pindah layar sekarang -- gak ada cara "
                "menghentikannya lebih cepat. Hasilnya nanti otomatis "
                "kelihatan begitu selesai. Ketuk nama mesin di pojok kiri "
                "atas kapan aja buat balik lihat progressnya."
            )
        elif stage == PAGE_CHECKING:
            QMessageBox.information(
                self, "Tetap Berjalan di Latar Belakang",
                "Sesi Check TETAP lanjut di alat sampai 60 detik selesai, "
                "walau kamu pindah layar sekarang. Hasilnya nanti otomatis "
                "muncul di Beranda begitu selesai. Ketuk nama mesin di "
                "pojok kiri atas kapan aja buat balik lihat progressnya."
            )
        self._change_page(target_page)

    # ---------------- Pilih slot -> loading ----------------

    def _on_slot_selected(self, idx):
        # BUG YANG DIPERBAIKI: kalau ada Kalibrasi/Check yang lagi jalan di
        # LATAR BELAKANG (user pencet "Lihat Nanti" terus balik ke Pilih
        # Slot), nge-klik kartu slot yang SAMA dulu bikin seluruh alur
        # restart dari 0 lagi -- kirim ulang command slot, balik ke Loading
        # 5 detik, dan kalau sampai pencet "Mulai Kalibrasi" lagi,
        # _calib_seconds_left di-set ulang ke 180. Tampilannya jadi
        # kelihatan "kok mundur ke awal", padahal firmware-nya sendiri
        # tetap lanjut jalan terus di baliknya -- cuma TAMPILAN dashboard
        # yang keliru ke-reset. Sekarang: kalau slot yang diklik SAMA
        # dengan yang lagi diproses, langsung lompat balik ke halaman
        # prosesnya (SAMA kayak nge-klik header), bukan mulai dari 0 lagi.
        if self._active_flow_page is not None:
            if idx == self.selected_slot_idx:
                self._change_page(self._active_flow_page)
            else:
                # Alat cuma punya SATU sensor fisik aktif dalam satu waktu.
                # Ganti slot SEKARANG (kirim command '0'-'9' beda) di
                # tengah Kalibrasi/Check bakal beneran mengganti slot aktif
                # di firmware DI TENGAH proses -- ngerusak/nyampur baseline
                # yang lagi direkam buat slot sebelumnya. Makanya diblokir
                # total, bukan dibiarkan jalan lalu hasilnya ngaco.
                QMessageBox.warning(
                    self, "Masih Ada Proses Berjalan",
                    f"{self._slot_display_name(self.selected_slot_idx)} masih dalam proses "
                    f"Kalibrasi/Check. Selesaikan dulu sebelum pindah ke mesin lain -- "
                    f"alat cuma bisa fokus ke satu mesin dalam satu waktu."
                )
            return

        slot = SLOT_DEFS[idx]
        self.selected_slot_idx = idx
        # Urutan command PENTING: pilih slot dulu, baru klaster RPM, baru
        # jenis bearing -- lihat main.ino di firmware.
        self._send_command(slot["slot_cmd"])
        self._send_command(slot["cluster_cmd"])
        self._send_command(slot["bearing_cmd"])
        self.lbl_machine_active.setText(f"⚙️  {self._slot_display_name(idx)}")

        # Beranda sekarang baca self.last_check_results[idx] sendiri (lihat
        # _render_beranda di page_awam.py) -- gak perlu lagi "buang" hasil
        # lama pas ganti slot, karena tiap slot punya hasil terakhirnya
        # SENDIRI-SENDIRI yang tersimpan permanen (lihat
        # _load_last_check_results). Ganti-ganti slot sekarang gak lagi
        # bikin hasil Check mesin lain "hilang" dari layar.
        self._render_beranda()

        # DULU di sini langsung lompat ke _begin_slot_flow() (Loading lalu
        # nebak sendiri Kalibrasi/Check dari status firmware) -- user
        # protes itu "ujug-ujug Check" tanpa ditanya dulu. Sekarang cuma
        # pindah ke halaman Pilih Aksi (_page_pilih_aksi), user yang pencet
        # sendiri mau Kalibrasi atau Check.
        self._refresh_pilih_aksi_page()
        self._change_page(PAGE_PILIH_AKSI)

    def _begin_slot_flow(self):
        self._set_nav_locked(True)
        self._active_flow_page = PAGE_LOADING
        slot = SLOT_DEFS[self.selected_slot_idx]
        self.lbl_loading_text.setText(f"Menyiapkan {slot['label']}...")
        self._change_page(PAGE_LOADING)

        self._flow_seconds_left = SLOT_SELECT_SETTLE_S
        self.lbl_loading_countdown.setText(str(self._flow_seconds_left))

        self._stop_flow_timer()
        self.flow_timer = QTimer(self)
        self.flow_timer.timeout.connect(self._loading_tick)
        self.flow_timer.start(1000)

    def _loading_tick(self):
        self._flow_seconds_left -= 1
        self.lbl_machine_active.setText(
            f"⏳ {self._slot_display_name(self.selected_slot_idx)} — {self._flow_seconds_left}s"
        )
        if self._flow_seconds_left <= 0:
            self._stop_flow_timer()
            self._proceed_pending_action()
        else:
            self.lbl_loading_countdown.setText(str(self._flow_seconds_left))

    def _proceed_pending_action(self):
        """Dipanggil begitu Loading (5 detik settle) kelar. BEDA dari versi
        lama (_branch_after_loading, sekarang dihapus): dulu di titik ini
        kode NEBAK SENDIRI mau Kalibrasi atau Check dari status firmware.
        Sekarang user udah pencet tombolnya sendiri di halaman Pilih Aksi
        (self._pending_action diisi 'calibrate' atau 'check' dari situ) --
        titik ini cuma MENJALANKAN pilihan itu, bukan mutusin.

        Satu pengecekan tetap dipertahankan buat kasus Check: kalau
        ternyata firmware masih lapor "Calibrating" (baseline-nya beneran
        belum/tidak ada -- misalnya baru dihapus dari sesi lain, atau
        catatan lokal ketinggalan), Check DIBATALKAN dan user dikasih
        tahu, bukan dipaksa jalan di atas baseline kosong yang hasilnya
        bakal ngaco."""
        status_key = (self.current_status_device or "").strip().lower()
        if self._pending_action == "calibrate":
            self._start_calibration_flow()
        elif self._pending_action == "check":
            if status_key == STATUS_CALIBRATING:
                self._set_nav_locked(False)
                self._active_flow_page = None
                self._restore_machine_active_label()
                if self.slot_calibrated.get(self.selected_slot_idx, False):
                    self.slot_calibrated[self.selected_slot_idx] = False
                    self._save_slot_calib_state()
                    self._refresh_slot_cards()
                    self._refresh_pilih_aksi_page()
                QMessageBox.warning(
                    self, "Belum Ada Baseline",
                    "Alat melaporkan slot ini ternyata belum/tidak lagi punya "
                    "baseline kalibrasi. Check dibatalkan -- jalankan "
                    "Kalibrasi dulu."
                )
                self._change_page(PAGE_MACHINE_SELECT)
            else:
                # BUKTI LANGSUNG dari ESP32 kalau baseline beneran ada --
                # sinkronkan catatan lokal kalau ternyata belum sinkron.
                if not self.slot_calibrated.get(self.selected_slot_idx, False):
                    self.slot_calibrated[self.selected_slot_idx] = True
                    self._save_slot_calib_state()
                    self._refresh_slot_cards()
                    self._refresh_pilih_aksi_page()
                self._start_check_flow()
        else:
            self._change_page(PAGE_MACHINE_SELECT)

    # ---------------- Kalibrasi (180 detik, atau sesuai config.py) ----------------

    def _start_calibration_flow(self):
        self._set_nav_locked(True)
        self._active_flow_page = PAGE_CALIBRATING
        self._calib_grace_active = False
        # BARU: penanda "apa firmware PERNAH beneran bilang 'Calibrating'
        # buat sesi kalibrasi INI". Lihat catatan panjang di
        # _calibration_tick soal kenapa ini perlu -- tanpa ini, kalibrasi
        # bisa "selesai" cuma sedetik setelah dipencet.
        self._calib_confirmed_started = False
        self.lbl_calib_slot.setText(self._slot_display_name(self.selected_slot_idx))
        # SENGAJA tidak kirim 'R' di sini. ESP32 sudah otomatis memulai
        # kalibrasi sendiri begitu slot kosong dipilih (lihat
        # selectMachineBaselineSlot() di main.ino) -- dashboard cukup
        # MENAMPILKAN halaman kalibrasi, tidak perlu menyuruh ulang.
        # Command 'R' tetap dipakai, tapi khusus untuk tombol "KALIBRASI"
        # manual di header (re-kalibrasi slot yang SUDAH ada baseline-nya).
        self._change_page(PAGE_CALIBRATING)

        self._calib_seconds_left = CALIBRATION_DURATION_S
        self._update_calib_label()

        self._stop_flow_timer()
        self.flow_timer = QTimer(self)
        self.flow_timer.timeout.connect(self._calibration_tick)
        self.flow_timer.start(1000)

    def _calibration_tick(self):
        # PERBAIKAN PENTING (bug lama): dulu ada "jaring pengaman" yang
        # nyatet kalibrasi SELESAI begitu angka countdown 180 detik habis --
        # TIDAK PEDULI status asli dari ESP32. Itu "gali lubang tutup
        # lubang": kalau paket status kebetulan lewat / kabel USB nyendat
        # sebentar, dashboard bisa nyatet "sudah terkalibrasi" padahal
        # baseline di alat BELUM beneran kelar. Sekarang satu-satunya cara
        # dashboard bilang "kalibrasi selesai" adalah kalau ESP32 SENDIRI
        # ngirim status yang BUKAN "Calibrating" lagi -- gak ada jalan
        # pintas lain.
        status_key = (self.current_status_device or "").strip().lower()

        # BUG KEDUA (baru ketemu, ini yang bikin "sekejap langsung
        # tersimpan"): self.current_status_device itu SATU variabel yang
        # terus dipakai ulang -- begitu kamu pencet "Mulai Kalibrasi", nilai
        # di variabel itu MASIH nilai LAMA dari sebelum kamu pencet (misal
        # "Normal", sisa dari kalibrasi/Check sebelumnya) sampai paket JSON
        # BARU dari ESP32 beneran datang bilang "Calibrating". Kalau tick
        # PERTAMA (1 detik pertama) ini kebetulan jalan SEBELUM paket baru
        # itu sempat sampai, kode di atas bakal baca status LAMA yang bukan
        # "Calibrating", langsung nganggep "beres" -- padahal kalibrasinya
        # bahkan belum sempat mulai. Makanya sekarang WAJIB pernah lihat
        # firmware bilang "Calibrating" MINIMAL SATU KALI dulu (dicatat di
        # self._calib_confirmed_started) sebelum status "bukan Calibrating"
        # boleh dianggap tanda selesai.
        if status_key == STATUS_CALIBRATING:
            self._calib_confirmed_started = True

        if self._calib_confirmed_started and status_key != STATUS_CALIBRATING:
            self._stop_flow_timer()
            self._finish_calibration()
            return

        self._calib_seconds_left -= 1
        if self._calib_seconds_left > 0:
            self._update_calib_label()
            return

        # Countdown normal (180 detik, harusnya cukup) sudah habis TAPI
        # ESP32 masih lapor "Calibrating". Daripada pura-pura selesai (bug
        # lama), dashboard kasih waktu tambahan (grace period) sambil
        # TERUS TERANG bilang ke user kalau ini nunggu konfirmasi alat --
        # bukan diam-diam ditutupi.
        if not getattr(self, "_calib_grace_active", False):
            self._calib_grace_active = True
            self._calib_grace_left = 20
            self.lbl_calib_countdown.setText("...")
            self.lbl_machine_active.setText(
                f"⏳ {self._slot_display_name(self.selected_slot_idx)} — menunggu konfirmasi alat"
            )
            return

        self._calib_grace_left -= 1
        if self._calib_grace_left > 0:
            return

        # Sudah 200 detik total (180 + 20 grace) dan ESP32 masih belum
        # ganti status dari "Calibrating". Ini kemungkinan besar masalah
        # KOMUNIKASI (kabel/port serial), bukan berarti kalibrasi di alat
        # gagal. Dashboard TIDAK menandai slot ini "sudah terkalibrasi" --
        # itu akan jadi data bohong. User diberi tahu apa adanya dan
        # diarahkan buat cek koneksi lalu pilih ulang slotnya.
        self._stop_flow_timer()
        self._calib_grace_active = False
        self._set_nav_locked(False)
        self._active_flow_page = None
        self._restore_machine_active_label()
        QMessageBox.warning(
            self, "Kalibrasi Belum Dikonfirmasi Selesai",
            "Sudah 200 detik tapi alat masih melaporkan status \"Calibrating\". "
            "Ini KEMUNGKINAN masalah komunikasi serial (kabel USB longgar / "
            "port nyendat), BUKAN berarti kalibrasi di alat gagal atau belum "
            "jalan. Baseline SENGAJA tidak ditandai selesai di sini supaya "
            "gak ada data yang salah -- cek kabel/koneksi ke alat, lalu pilih "
            "slot ini lagi buat lihat status terbarunya."
        )
        self._change_page(PAGE_MACHINE_SELECT)

    def _update_calib_label(self):
        secs = max(0, self._calib_seconds_left)
        m, s = divmod(secs, 60)
        self.lbl_calib_countdown.setText(f"{m}:{s:02d}")
        self.lbl_machine_active.setText(
            f"⏳ {self._slot_display_name(self.selected_slot_idx)} — Kalibrasi {m}:{s:02d}"
        )

    def _finish_calibration(self):
        self._set_nav_locked(False)
        self._active_flow_page = None
        self._restore_machine_active_label()
        self.slot_calibrated[self.selected_slot_idx] = True
        self._save_slot_calib_state()
        self._refresh_slot_cards()
        self._refresh_pilih_aksi_page()
        slot = SLOT_DEFS[self.selected_slot_idx]
        self.lbl_hasil_kalib_text.setText(
            f"Kalibrasi {slot['label']} selesai!\n"
            f"Data baseline sudah tersimpan permanen di alat (tahan mati listrik)."
        )
        self._change_page(PAGE_HASIL_KALIBRASI)

    # ---------------- Check (60 detik, atau sesuai config.py) ----------------

    def _start_check_flow(self):
        self._set_nav_locked(True)
        self._active_flow_page = PAGE_CHECKING
        slot_idx = self.selected_slot_idx
        slot_txt = SLOT_DEFS[slot_idx]['label'] if slot_idx >= 0 else "(slot belum dipilih)"
        self.lbl_check_slot.setText(slot_txt)

        self._send_command('K')  # mulai sesi Check di firmware
        self._reset_session()    # counter Riwayat mulai dari 0 buat sesi ini
        self.check_active = True
        self._change_page(PAGE_CHECKING)

        self._check_seconds_left = CHECK_DURATION_S
        self._update_check_label()

        self._stop_flow_timer()
        self.flow_timer = QTimer(self)
        self.flow_timer.timeout.connect(self._check_tick)
        self.flow_timer.start(1000)

    def _check_tick(self):
        self._check_seconds_left -= 1
        if self._check_seconds_left <= 0:
            self._stop_flow_timer()
            self.check_active = False
            self._send_command('P')  # firmware gak auto-kirim hasil, harus diminta
            self._finish_check()
        else:
            self._update_check_label()

    def _update_check_label(self):
        secs = max(0, self._check_seconds_left)
        m, s = divmod(secs, 60)
        self.lbl_check_countdown.setText(f"{m}:{s:02d}")
        self.lbl_machine_active.setText(
            f"🔍 {self._slot_display_name(self.selected_slot_idx)} — Check {m}:{s:02d}"
        )

    def _finish_check(self):
        self._set_nav_locked(False)
        self._active_flow_page = None
        self._restore_machine_active_label()
        # PENTING: pakai self.session_worst_status (kondisi TERPARAH selama
        # SELURUH 60 detik sesi Check), BUKAN self.current_status_device
        # (cuma snapshot detik terakhir doang). Kalau pakai snapshot detik
        # terakhir, bisa kejadian kayak yang ketangkap di alat asli: 37x
        # Bahaya selama sesi, tapi kebetulan pas detik ke-60 mesinnya lagi
        # Normal -- jadi judul hasil salah bilang "NORMAL - Aman" padahal
        # sesi itu sebenarnya parah. Headline HARUS mencerminkan kondisi
        # terburuk yang pernah terjadi, bukan momen terakhir yang kebetulan.
        #
        # BUG KEDUA yang baru dibenerin (mirip banget sama yang di atas):
        # self.session_worst_status di-set default "Normal" pas sesi Check
        # MULAI (lihat _reset_session di dashboard_core.py) -- itu cuma
        # placeholder "belum ketemu apa-apa yang parah", BUKAN bukti bahwa
        # datanya beneran normal. Kalau ESP32 gak pernah tersambung sama
        # sekali selama 60 detik itu (kabel USB kecabut, port salah, dst),
        # self.session_sample_count tetap 0 -- gak ada satupun data ASLI
        # yang pernah masuk -- tapi placeholder "Normal" tadi gak pernah
        # diganti, jadi hasilnya kelihatan "NORMAL - Aman" padahal
        # kenyataannya alat gak pernah ngirim apa-apa. Makanya sekarang
        # dicek dulu: kalau sample_count-nya 0, itu BUKAN "Normal", itu
        # "gak ada data sama sekali" -- 2 hal yang beda, harus dibedain,
        # bukan dianggap sama.
        #
        # BUG KETIGA (kelas yang sama lagi): self.session_worst_status pakai
        # perbandingan "severity" (lihat STATUS_SEVERITY di config.py:
        # diam=-1, normal=0, waspada=1, bahaya=2). "Diam" severity-nya LEBIH
        # RENDAH dari "Normal" -- artinya kalau SELURUH 60 detik sesi itu
        # mesinnya diam total (gak pernah nyala), status "Diam" itu TIDAK
        # PERNAH menang lawan placeholder awal "Normal" (-1 gak lebih besar
        # dari 0), jadi hasilnya kelihatan "NORMAL - Aman" padahal mesinnya
        # gak pernah jalan sama sekali selama Check. Diperbaiki dengan cek
        # eksplisit: kalau SEMUA sample yang masuk itu "Diam" (gak ada satu
        # pun sample "Normal"/"Waspada"/"Bahaya"), hasilnya WAJIB "Diam",
        # bukan "Normal".
        if self.session_sample_count == 0:
            status_key = "nodata"
        elif self.session_bahaya_count > 0:
            status_key = "bahaya"
        elif self.session_waspada_count > 0:
            status_key = "waspada"
        elif self.session_diam_count >= self.session_sample_count:
            status_key = "diam"
        else:
            status_key = (self.session_worst_status or "normal").strip().lower()

        # Ini SATU-SATUNYA tempat self.last_check_results[slot] diisi.
        # Beranda & Rekomendasi (mode Awam) baca dari sini, BUKAN dari data
        # live -- jadi "hasil" di Beranda selalu berasal dari sesi Check
        # yang BENERAN sudah kelar, bukan angka yang masih terus berubah.
        # Disimpan per-slot (bukan 1 variabel global) dan langsung ditulis
        # ke disk -- lihat catatan di config.py (LAST_CHECK_FILE) soal
        # kenapa ini perlu supaya hasil gak "hilang" pas dashboard ditutup.
        self.last_check_results[self.selected_slot_idx] = {
            "slot_idx": self.selected_slot_idx,
            "status_key": status_key,
            "diag_label": self.current_diag_label,
            "servis": self.current_servis,
            "timestamp": datetime.now().strftime("%H:%M:%S"),
        }
        self._save_last_check_results()
        self._render_beranda()

        icon_map = {"bahaya": "⛔", "waspada": "⚠️", "diam": "⏸️", "nodata": "🔌"}
        col_map = {"bahaya": COL_BAD, "waspada": COL_WARN, "diam": COL_TEXT_DIM, "nodata": COL_BAD}
        text_map = {
            "bahaya": "BAHAYA - Periksa mesin sekarang",
            "waspada": "WASPADA - Perlu dipantau",
            "diam": "Mesin sedang mati",
            "nodata": "TIDAK ADA DATA - Alat tidak tersambung",
        }

        self.lbl_hasil_check_icon.setText(icon_map.get(status_key, "✅"))
        self.lbl_hasil_check_status.setText(text_map.get(status_key, "NORMAL - Aman"))
        self.lbl_hasil_check_status.setStyleSheet(
            f"font-size: 13px; font-weight: bold; color: {col_map.get(status_key, COL_OK)};"
        )
        if status_key == "nodata":
            self.lbl_hasil_check_detail.setText(
                "Selama 60 detik sesi ini, TIDAK ADA satu data pun yang masuk dari alat. "
                "Ini BUKAN hasil \"Normal\" -- ceknya sama sekali gak kejadian karena kabel "
                "USB/koneksi ke ESP32 kemungkinan lepas atau alatnya mati. Cek koneksi lalu "
                "ulangi Check."
            )
        else:
            self.lbl_hasil_check_detail.setText(
                f"Selama sesi ini: {self.session_sample_count} data terekam, "
                f"{self.session_waspada_count}x Waspada, {self.session_bahaya_count}x Bahaya. "
                f"Kondisi terparah: {self.session_worst_status}."
            )
        self._change_page(PAGE_HASIL_CHECK)

    # ---------------- Hapus baseline (command Z) ----------------

    def _confirm_delete_baseline(self):
        if self.selected_slot_idx < 0:
            QMessageBox.warning(self, "Perhatian", "Pilih slot dulu sebelum menghapus baseline.")
            return
        slot = SLOT_DEFS[self.selected_slot_idx]
        reply = QMessageBox.question(
            self, "Hapus Baseline",
            f"Yakin mau hapus baseline & riwayat check {slot['label']}?\n\n"
            f"Data ini akan HILANG PERMANEN dan TIDAK BISA dikembalikan lagi.",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No
        )
        if reply == QMessageBox.Yes:
            self._send_command('Z')
            self.slot_calibrated[self.selected_slot_idx] = False
            self._save_slot_calib_state()
            self._refresh_slot_cards()
            self._refresh_pilih_aksi_page()
            # 'Z' di firmware hapus BASELINE *dan* ringkasan cek terakhir
            # buat slot itu (lihat deleteCheckSummaryFromFlash di
            # main.ino) -- catatan lokal kita ikutin, jangan sampai
            # Beranda masih nunjukin hasil Check lama yang dihitung dari
            # baseline yang udah gak ada lagi.
            if self.selected_slot_idx in self.last_check_results:
                del self.last_check_results[self.selected_slot_idx]
                self._save_last_check_results()
            self._render_beranda()
