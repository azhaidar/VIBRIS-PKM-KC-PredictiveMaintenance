from PyQt5.QtWidgets import (
    QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush
import pyqtgraph as pg
from config import COL_PANEL_DARK, COL_TEXT_LIGHT, COL_TEXT_DIM, COL_ACCENT
from widgets.gauge import GradientGauge


class SummaryPageMixin:
    """Method-method untuk halaman ini. Di-mixin ke class Dashboard
    (lihat dashboard_core.py) supaya semua method di sini bisa akses
    self.current_v, self.selected_slot_idx, dst -- gak ada state baru di sini,
    murni pemisahan biar dashboard_core.py gak sepanjang 2000 baris."""

    def _page_summary(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        self.lbl_sum_machine = QLabel("Target: Belum dipilih")
        self.lbl_sum_machine.setStyleSheet(f"font-size: 12px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(self.lbl_sum_machine)

        grid = QGridLayout()
        grid.setSpacing(8)

        self.gauge_s = GradientGauge("Sound", 30.0, 100.0, 75.0, 85.0, "dB")
        self.gauge_t = GradientGauge("Temperature", 20.0, 80.0, 42.0, 50.0, "°C")
        self.gauge_v = GradientGauge("Vibration", 0.0, 0.5, 0.18, 0.25, "G")

        grid.addWidget(self.gauge_s, 0, 0)
        grid.addWidget(self.gauge_t, 0, 1)
        grid.addWidget(self.gauge_v, 1, 0)

        # Dulu di sini cuma QFrame kosong (placeholder) yang gak pernah diisi.
        # Sekarang diisi ringkasan hasil diagnosa AI (Health Score, Fault Diag,
        # Estimasi Servis) -- data ini sebelumnya cuma tampil di tab RAW READING
        # (panel_ai), padahal justru ini nilai paling penting buat tab SUMMARY.
        self.panel_ai_sum = QFrame()
        self.panel_ai_sum.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border-radius: 4px;")
        lay_ai_sum = QVBoxLayout(self.panel_ai_sum)
        lay_ai_sum.setContentsMargins(4, 2, 4, 2)
        lay_ai_sum.setSpacing(2)

        lbl_ai_sum_title = QLabel("🤖 RINGKASAN DIAGNOSA")
        lbl_ai_sum_title.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {COL_ACCENT};")
        lay_ai_sum.addWidget(lbl_ai_sum_title)

        grid_ai_sum = QGridLayout()
        grid_ai_sum.setHorizontalSpacing(4)
        grid_ai_sum.setVerticalSpacing(1)

        def _ai_row_sum(r, label_text):
            lbl1 = QLabel(label_text)
            lbl1.setStyleSheet("font-size: 9px; color: #999;")
            lbl2 = QLabel("--")
            lbl2.setStyleSheet("font-size: 8px; font-weight: bold; color: #eee;")
            lbl2.setWordWrap(True)
            lbl2.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            grid_ai_sum.addWidget(lbl1, r, 0)
            grid_ai_sum.addWidget(lbl2, r, 1)
            return lbl2

        self.lbl_hs_val_sum = _ai_row_sum(0, "Health Score:")
        self.lbl_diag_val_sum = _ai_row_sum(1, "Fault Diag:")
        self.lbl_servis_val_sum = _ai_row_sum(2, "Estimasi Servis:")

        lay_ai_sum.addLayout(grid_ai_sum)
        lay_ai_sum.addStretch(1)

        grid.addWidget(self.panel_ai_sum, 1, 1)

        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)
        grid.setRowStretch(0, 1)
        grid.setRowStretch(1, 1)

        layout.addLayout(grid)
        layout.addStretch(1)

        self.lbl_diag_desc_summary = QLabel("Menunggu data deteksi...")
        self.lbl_diag_desc_summary.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_DIM}; border-top: 1px solid #333; padding-top: 4px;")
        self.lbl_diag_desc_summary.setWordWrap(True)
        self.lbl_diag_desc_summary.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.lbl_diag_desc_summary)

        return page

