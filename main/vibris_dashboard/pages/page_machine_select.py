from PyQt5.QtWidgets import (
    QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush
import pyqtgraph as pg
from config import COL_PANEL_DARK, COL_TEXT_LIGHT, COL_TEXT_DIM, COL_ACCENT, COL_ACCENT_DIM, COL_OK, SLOT_DEFS


class MachineSelectMixin:
    """Halaman pilih Slot mesin. SENGAJA cuma 2 tombol tetap (Slot A / Slot
    B, dari SLOT_DEFS di config.py) -- gak ada tombol tambah/hapus/nama
    custom kayak versi lama, karena (1) device ini gak punya keyboard fisik
    jadi gak ada cara wajar buat ngetik nama, dan (2) motor yang beneran ada
    sekarang cuma 2 klaster bearing. Kalau nanti nambah motor/klaster baru,
    tinggal tambah 1 entry baru di SLOT_DEFS -- gak perlu ubah halaman ini.

    Klik salah satu slot memicu _on_slot_selected di page_flow.py: kirim
    command slot+klaster+bearing ke ESP32, lalu pindah ke halaman Pilih
    Aksi (_page_pilih_aksi) -- DI SITU user pencet sendiri mau Kalibrasi
    atau Check, gak ada tebakan otomatis lagi (dulu ada, sudah dihapus
    karena user gak suka "ujug-ujug Check" tanpa ditanya)."""

    def _page_machine_select(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(8)

        lbl_title = QLabel("Pilih Slot Mesin")
        lbl_title.setAlignment(Qt.AlignCenter)
        lbl_title.setStyleSheet(f"font-size: 12px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(lbl_title)

        lbl_hint = QLabel(
            "Pilih salah satu, lalu di layar berikutnya kamu pilih sendiri "
            "mau Kalibrasi (rekam baseline baru) atau Check (bandingkan "
            "dengan baseline)."
        )
        lbl_hint.setAlignment(Qt.AlignCenter)
        lbl_hint.setWordWrap(True)
        lbl_hint.setStyleSheet(f"font-size: 8px; color: {COL_TEXT_DIM};")
        layout.addWidget(lbl_hint)

        grid = QGridLayout()
        grid.setSpacing(10)
        # Simpan referensi tiap label badge di sini (bukan cuma di dalam
        # tombol) supaya nanti BISA diubah lagi setelah kartu ini dibuat --
        # lihat catatan panjang di _make_slot_card soal kenapa ini perlu.
        self.slot_card_cluster_lbls = {}
        for i, slot in enumerate(SLOT_DEFS):
            grid.addWidget(self._make_slot_card(i, slot), 0, i)
        layout.addLayout(grid, 1)

        return page

    def _refresh_slot_cards(self):
        """Update ulang teks+warna badge kalibrasi di kartu pilih slot.

        KENAPA INI PERLU ADA (bug yang baru diperbaiki): halaman Pilih Slot
        cuma dibangun SEKALI waktu aplikasi baru nyala (lihat dashboard_core.py
        __init__ -> self.stack.addWidget(self._page_machine_select())).
        Setelah itu, pindah ke halaman ini cuma manggil
        self.stack.setCurrentIndex(4) -- widget yang UDAH ada dipakai lagi,
        gak dibangun ulang. Jadi kalau badge "✓ Sudah Terkalibrasi" ditulis
        cuma di dalam _make_slot_card() waktu __init__, terus user kalibrasi
        slot itu SETELAH aplikasi jalan, badge-nya bakal keliatan "Kosong"
        terus selama-lamanya walau kalibrasinya beneran udah selesai --
        soalnya gak ada kode yang nulis ulang teksnya.

        Perbaikannya: simpen referensi widget label-nya (self.slot_card_cluster_lbls,
        diisi di _make_slot_card), terus panggil method INI setiap kali status
        kalibrasi berubah beneran -- yaitu di _finish_calibration() (kalibrasi
        baru selesai) dan _confirm_delete_baseline() (baseline dihapus), dua-duanya
        di page_flow.py. Method ini gak nebak apa-apa, cuma baca ulang
        self.slot_calibrated (sumber data yang sama dipakai waktu __init__)
        dan nulis ulang teks+warna tiap label yang udah tersimpan.
        """
        for idx, lbl in getattr(self, "slot_card_cluster_lbls", {}).items():
            is_calibrated = getattr(self, "slot_calibrated", {}).get(idx, False)
            lbl.setText("✓ Sudah Terkalibrasi" if is_calibrated else "○ Kosong -- belum ada baseline")
            lbl.setStyleSheet(
                f"font-size: 8px; font-weight: bold; "
                f"color: {COL_OK if is_calibrated else COL_TEXT_DIM}; "
                f"background: transparent; border: none;"
            )

    def _make_slot_card(self, idx, slot):
        btn = QPushButton()
        btn.setFixedHeight(110)
        lay = QVBoxLayout(btn)
        lay.setContentsMargins(4, 4, 4, 4)
        lay.setSpacing(3)

        lbl_icon = QLabel("⚙️")
        lbl_icon.setAlignment(Qt.AlignCenter)
        lbl_icon.setStyleSheet("font-size: 24px; background: transparent; border: none;")

        lbl_name = QLabel(slot["label"])
        lbl_name.setAlignment(Qt.AlignCenter)
        lbl_name.setStyleSheet(
            f"font-size: 13px; font-weight: bold; color: {COL_TEXT_LIGHT}; "
            f"background: transparent; border: none;"
        )

        # SENGAJA gak nampilin slot["cluster_label"] ("Klaster A (~1400 RPM)")
        # di sini -- itu jargon teknis buat hitung frekuensi fault bearing,
        # gak relevan buat pemilik UMKM yang cuma mau tahu "mesin yang mana"
        # yang mau dicek. Info klaster lengkap tetap ada, cuma dipindah ke
        # tempat yang memang buat teknisi (mode Teknisi & tombol DEBUG).
        #
        # Sebagai gantinya, di sini nampilin status kalibrasi slot -- ini
        # jawaban buat "gimana caraku tau ini udah dikalibrasi apa belum
        # SEBELUM milih": badge ini keliatan duluan di sini, sebelum masuk
        # ke halaman Pilih Aksi tempat kamu manual milih Kalibrasi/Check.
        # Sumber datanya catatan lokal dashboard (self.slot_calibrated) --
        # lihat _load_slot_calib_state, dan disinkronkan ke bukti asli dari
        # ESP32 di _proceed_pending_action (page_flow.py) tiap kali ada
        # bukti baru.
        is_calibrated = getattr(self, "slot_calibrated", {}).get(idx, False)
        cluster_text = "✓ Sudah Terkalibrasi" if is_calibrated else "○ Kosong -- belum ada baseline"
        cluster_color = COL_OK if is_calibrated else COL_TEXT_DIM
        lbl_cluster = QLabel(cluster_text)
        lbl_cluster.setAlignment(Qt.AlignCenter)
        lbl_cluster.setWordWrap(True)
        lbl_cluster.setStyleSheet(
            f"font-size: 8px; font-weight: bold; color: {cluster_color}; background: transparent; border: none;"
        )

        selected = (idx == getattr(self, "selected_slot_idx", -1))
        border_col = COL_ACCENT if selected else "#2a3542"
        bg = COL_ACCENT_DIM if selected else COL_PANEL_DARK

        lay.addWidget(lbl_icon)
        lay.addWidget(lbl_name)
        lay.addWidget(lbl_cluster)

        btn.setStyleSheet(
            f"QPushButton {{ background-color: {bg}; border: 2px solid {border_col}; border-radius: 6px; }}"
            f"QPushButton:hover {{ border: 2px solid {COL_ACCENT}; }}"
        )
        btn.clicked.connect(lambda checked, i=idx: self._on_slot_selected(i))
        return btn
