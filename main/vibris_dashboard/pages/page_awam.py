from PyQt5.QtWidgets import (
    QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush
import pyqtgraph as pg
from config import (
    COL_PANEL_DARK, COL_TEXT_LIGHT, COL_TEXT_DIM, COL_ACCENT, COL_ACCENT_DIM,
    COL_OK, COL_WARN, COL_BAD, COL_IDLE, DIAG_LABEL_ID_MAP,
)


class AwamPagesMixin:
    """Method-method untuk halaman ini. Di-mixin ke class Dashboard
    (lihat dashboard_core.py) supaya semua method di sini bisa akses
    self.current_v, self.selected_slot_idx, dst -- gak ada state baru di sini,
    murni pemisahan biar dashboard_core.py gak sepanjang 2000 baris."""

    def _page_beranda(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(6)

        self.lbl_beranda_machine = QLabel("Mesin: Belum dipilih")
        self.lbl_beranda_machine.setStyleSheet(f"font-size: 12px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        self.lbl_beranda_machine.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.lbl_beranda_machine)

        self.frame_beranda_status = QFrame()
        self.frame_beranda_status.setStyleSheet(
            f"background-color: {COL_PANEL_DARK}; border-radius: 10px; border: 2px solid {COL_TEXT_DIM};"
        )
        status_lay = QVBoxLayout(self.frame_beranda_status)
        status_lay.setContentsMargins(8, 14, 8, 14)
        status_lay.setSpacing(4)

        self.lbl_beranda_icon = QLabel("●")
        self.lbl_beranda_icon.setStyleSheet(f"font-size: 34px; color: {COL_TEXT_DIM};")
        self.lbl_beranda_icon.setAlignment(Qt.AlignCenter)
        status_lay.addWidget(self.lbl_beranda_icon)

        self.lbl_beranda_status = QLabel("MENUNGGU DATA...")
        self.lbl_beranda_status.setStyleSheet(f"font-size: 15px; font-weight: bold; color: {COL_TEXT_DIM};")
        self.lbl_beranda_status.setAlignment(Qt.AlignCenter)
        status_lay.addWidget(self.lbl_beranda_status)

        self.lbl_beranda_desc = QLabel("Sedang menyambungkan ke alat...")
        self.lbl_beranda_desc.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_LIGHT};")
        self.lbl_beranda_desc.setAlignment(Qt.AlignCenter)
        self.lbl_beranda_desc.setWordWrap(True)
        status_lay.addWidget(self.lbl_beranda_desc)

        layout.addWidget(self.frame_beranda_status, 1)

        self.lbl_beranda_servis = QLabel("Estimasi servis berikutnya: --")
        self.lbl_beranda_servis.setStyleSheet(
            f"font-size: 9px; color: {COL_TEXT_LIGHT}; background-color: {COL_ACCENT_DIM}; "
            f"padding: 5px; border-radius: 4px;"
        )
        self.lbl_beranda_servis.setAlignment(Qt.AlignCenter)
        self.lbl_beranda_servis.setWordWrap(True)
        layout.addWidget(self.lbl_beranda_servis)

        return page


    def _page_riwayat(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        lbl_title = QLabel("Riwayat Sesi Pemantauan Ini")
        lbl_title.setStyleSheet(f"font-size: 11px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(lbl_title)

        grid = QGridLayout()
        grid.setSpacing(6)

        def _stat_box(label_text):
            frame = QFrame()
            frame.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border-radius: 4px;")
            lay = QVBoxLayout(frame)
            lay.setContentsMargins(4, 4, 4, 4)
            lay.setSpacing(1)
            lbl_top = QLabel(label_text)
            lbl_top.setStyleSheet("font-size: 9px; color: #999;")
            lbl_top.setAlignment(Qt.AlignCenter)
            lbl_val = QLabel("0")
            lbl_val.setStyleSheet(f"font-size: 15px; font-weight: bold; color: {COL_TEXT_LIGHT};")
            lbl_val.setAlignment(Qt.AlignCenter)
            lay.addWidget(lbl_top)
            lay.addWidget(lbl_val)
            return frame, lbl_val

        box_waspada, self.lbl_riwayat_waspada = _stat_box("Kali Waspada")
        box_bahaya, self.lbl_riwayat_bahaya = _stat_box("Kali Bahaya")
        box_sample, self.lbl_riwayat_sample = _stat_box("Total Data Masuk")

        grid.addWidget(box_waspada, 0, 0)
        grid.addWidget(box_bahaya, 0, 1)
        grid.addWidget(box_sample, 0, 2)
        layout.addLayout(grid)

        lbl_list_title = QLabel("Kejadian Terakhir:")
        lbl_list_title.setStyleSheet("font-size: 8px; font-weight: bold; color: #ccc;")
        layout.addWidget(lbl_list_title)

        self.list_riwayat = QListWidget()
        self.list_riwayat.setStyleSheet("background-color: #ffffff; color: #222222; font-size: 8px;")
        self.list_riwayat.addItem("Belum ada kejadian pada sesi ini.")
        layout.addWidget(self.list_riwayat, 1)

        lbl_hint = QLabel("* Angka di atas otomatis mulai dari 0 tiap kali kamu pilih slot mesin untuk Check baru (durasi 1 menit).")
        lbl_hint.setStyleSheet("font-size: 9px; color: #888; font-style: italic;")
        lbl_hint.setWordWrap(True)
        layout.addWidget(lbl_hint)

        return page


    def _page_rekomendasi(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        lbl_title = QLabel("Rekomendasi Tindakan")
        lbl_title.setStyleSheet(f"font-size: 11px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(lbl_title)

        self.frame_rekom = QFrame()
        self.frame_rekom.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border-radius: 6px;")
        rekom_lay = QVBoxLayout(self.frame_rekom)
        rekom_lay.setContentsMargins(8, 8, 8, 8)
        rekom_lay.setSpacing(6)

        self.lbl_rekom_penyebab = QLabel("Kemungkinan penyebab: --")
        self.lbl_rekom_penyebab.setStyleSheet(f"font-size: 10px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        self.lbl_rekom_penyebab.setWordWrap(True)
        rekom_lay.addWidget(self.lbl_rekom_penyebab)

        self.lbl_rekom_tindakan = QLabel("Tindakan yang disarankan: --")
        self.lbl_rekom_tindakan.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_DIM};")
        self.lbl_rekom_tindakan.setWordWrap(True)
        rekom_lay.addWidget(self.lbl_rekom_tindakan)

        rekom_lay.addStretch(1)
        layout.addWidget(self.frame_rekom, 1)

        lbl_disclaimer = QLabel(
            "* Rekomendasi otomatis berdasarkan pola sensor. Untuk kepastian, tetap periksa mesin secara langsung."
        )
        lbl_disclaimer.setStyleSheet("font-size: 9px; color: #888; font-style: italic;")
        lbl_disclaimer.setWordWrap(True)
        layout.addWidget(lbl_disclaimer)

        return page


    def _build_rekomendasi_text(self):
        """PENTING: dibaca dari self.last_check_results[slot yang lagi
        aktif] (hasil sesi Check yang SUDAH SELESAI untuk slot itu), bukan
        dari data live (self.current_status_device dkk). Kalau ini balik
        dibaca dari data live, halaman Rekomendasi ikut jadi "real-time"
        lagi walau user belum pernah Check -- itu persis bug yang lagi
        dibenerin (lihat _render_beranda di bawah)."""
        result = self.last_check_results.get(self.selected_slot_idx)
        if result is None:
            return "belum ada hasil Check", "pilih slot mesin lalu jalankan sesi Check (1 menit) dulu"

        status_key = result["status_key"]
        label_key = (result["diag_label"] or "").strip().lower()
        penyebab = DIAG_LABEL_ID_MAP.get(label_key, result["diag_label"] or "Belum ada data diagnosa")

        if status_key == "bahaya":
            tindakan = "STOP mesin sekarang, jangan dioperasikan dulu, dan hubungi teknisi secepatnya."
        elif status_key == "waspada":
            tindakan = f"Mesin masih bisa jalan, tapi jadwalkan pemeriksaan dalam waktu dekat (estimasi servis: {result['servis']})."
        elif status_key == "diam":
            tindakan = "Mesin sedang tidak beroperasi saat sesi Check terakhir."
        elif status_key == "nodata":
            penyebab = "alat tidak mengirim data sama sekali selama sesi Check terakhir (bukan hasil pemeriksaan)"
            tindakan = "Cek kabel USB/koneksi ke ESP32, pastikan alat menyala, lalu ulangi Check."
        else:
            tindakan = "Tidak ada tindakan yang diperlukan -- mesin normal pada sesi Check terakhir."

        return penyebab, tindakan


    def _render_beranda(self):
        """Gambar ulang halaman Beranda, MURNI dari
        self.last_check_results[slot yang lagi aktif] (hasil sesi Check
        terakhir yang beneran sudah selesai UNTUK SLOT ITU -- tiap slot
        punya hasil sendiri-sendiri, gak saling timpa). Dipanggil dari 3
        tempat: (1) sekali di akhir __init__ (dashboard_core.py) buat
        nunjukin hasil tersimpan (atau placeholder "belum ada hasil") pas
        alat baru nyala, (2) dari _finish_check() di page_flow.py tiap
        kali sesi Check baru saja kelar, (3) dari _on_slot_selected tiap
        ganti slot, biar Beranda nunjukin hasil punya slot yang SEKARANG
        aktif. TIDAK dipanggil dari _update_gui() -- itu sebabnya Beranda
        gak lagi ikut ter-update tiap 200ms dari data live."""
        machine_name = self._slot_display_name(self.selected_slot_idx)
        self.lbl_beranda_machine.setText(f"Mesin: {machine_name}")

        result = self.last_check_results.get(self.selected_slot_idx)
        if result is None:
            self.lbl_beranda_icon.setText("❔")
            self.lbl_beranda_icon.setStyleSheet(f"font-size: 34px; color: {COL_TEXT_DIM};")
            self.lbl_beranda_status.setText("BELUM ADA HASIL CHECK")
            self.lbl_beranda_status.setStyleSheet(f"font-size: 15px; font-weight: bold; color: {COL_TEXT_DIM};")
            self.lbl_beranda_desc.setText(
                "Pilih slot mesin, lalu jalankan sesi Check (1 menit) untuk melihat hasilnya di sini."
            )
            self.frame_beranda_status.setStyleSheet(
                f"background-color: {COL_PANEL_DARK}; border-radius: 10px; border: 2px solid {COL_TEXT_DIM};"
            )
            self.lbl_beranda_servis.setText("Estimasi servis berikutnya: --")
        else:
            status_key = result["status_key"]
            icon = {"bahaya": "⛔", "waspada": "⚠️", "diam": "⏸️", "nodata": "🔌"}.get(status_key, "✅")
            col = {"bahaya": COL_BAD, "waspada": COL_WARN, "diam": COL_IDLE, "nodata": COL_BAD}.get(status_key, COL_OK)
            text = {
                "bahaya": "BAHAYA - PERIKSA SEKARANG",
                "waspada": "WASPADA - PERLU DIPANTAU",
                "diam": "MESIN SEDANG MATI (saat Check terakhir)",
                "nodata": "TIDAK ADA DATA (alat tidak tersambung)",
            }.get(status_key, "AMAN")
            desc = {
                "bahaya": "Sesi Check terakhir mendeteksi anomali serius. Matikan mesin dan hubungi teknisi.",
                "waspada": "Sesi Check terakhir menunjukkan tanda-tanda awal masalah. Sebaiknya diperiksa dalam waktu dekat.",
                "diam": "Mesin sedang tidak beroperasi saat sesi Check terakhir dijalankan.",
                "nodata": "Alat TIDAK mengirim data sama sekali selama sesi Check terakhir -- ini bukan hasil pemeriksaan, cek koneksi lalu ulangi Check.",
            }.get(status_key, "Semua parameter normal pada sesi Check terakhir.")

            self.lbl_beranda_icon.setText(icon)
            self.lbl_beranda_icon.setStyleSheet(f"font-size: 34px; color: {col};")
            self.lbl_beranda_status.setText(text)
            self.lbl_beranda_status.setStyleSheet(f"font-size: 15px; font-weight: bold; color: {col};")
            self.lbl_beranda_desc.setText(f"{desc}\nHasil Check pukul {result['timestamp']}.")
            self.frame_beranda_status.setStyleSheet(
                f"background-color: {COL_PANEL_DARK}; border-radius: 10px; border: 2px solid {col};"
            )
            self.lbl_beranda_servis.setText(f"Estimasi servis berikutnya: {result['servis']}")

        penyebab, tindakan = self._build_rekomendasi_text()
        self.lbl_rekom_penyebab.setText(f"Kemungkinan penyebab: {penyebab}")
        self.lbl_rekom_tindakan.setText(f"Tindakan yang disarankan: {tindakan}")

