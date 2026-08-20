from PyQt5.QtWidgets import (
    QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush
import pyqtgraph as pg
from config import COL_PANEL_DARK, COL_WARN, COL_BAD, D2_THRESHOLD_WASPADA, D2_THRESHOLD_BAHAYA


class ProcessedPageMixin:
    """Method-method untuk halaman ini. Di-mixin ke class Dashboard
    (lihat dashboard_core.py) supaya semua method di sini bisa akses
    self.current_v, self.selected_slot_idx, dst -- gak ada state baru di sini,
    murni pemisahan biar dashboard_core.py gak sepanjang 2000 baris."""

    def _page_processed(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(4)

        self.lbl_proc_snapshot = QLabel("Menunggu data... | RPM: -- | D²: --")
        self.lbl_proc_snapshot.setStyleSheet("font-size: 9px; color: #999;")
        layout.addWidget(self.lbl_proc_snapshot)

        grid = QGridLayout()
        grid.setSpacing(4)
        pg.setConfigOptions(antialias=True)

        self.graph_fft = pg.PlotWidget(title="Frequency Spectrum (FFT)")
        self.graph_fft.setBackground(COL_PANEL_DARK)
        self.graph_fft.showGrid(x=True, y=True, alpha=0.3)
        self.curve_fft = self.graph_fft.plot(pen=pg.mkPen('#a855f7', width=1.5))
        grid.addWidget(self.graph_fft, 0, 0, 1, 2)

        self.graph_rpm = pg.PlotWidget(title="RPM Estimasi")
        self.graph_rpm.setBackground(COL_PANEL_DARK)
        self.graph_rpm.showGrid(x=True, y=True, alpha=0.3)
        self.curve_rpm = self.graph_rpm.plot(pen=pg.mkPen('#4da6ff', width=1.5))
        grid.addWidget(self.graph_rpm, 1, 0)

        self.graph_d2 = pg.PlotWidget(title="Mahalanobis D²")
        self.graph_d2.setBackground(COL_PANEL_DARK)
        self.graph_d2.showGrid(x=True, y=True, alpha=0.3)
        self.curve_d2 = self.graph_d2.plot(pen=pg.mkPen('#ff6666', width=1.5))
        
        line_waspada = pg.InfiniteLine(pos=D2_THRESHOLD_WASPADA, angle=0,
                                        pen=pg.mkPen(COL_WARN, width=1.5, style=Qt.DashLine))
        line_bahaya = pg.InfiniteLine(pos=D2_THRESHOLD_BAHAYA, angle=0,
                                       pen=pg.mkPen(COL_BAD, width=1.5, style=Qt.DashLine))
        self.graph_d2.addItem(line_waspada)
        self.graph_d2.addItem(line_bahaya)
        grid.addWidget(self.graph_d2, 1, 1)

        layout.addLayout(grid, 3)

        lbl_anomali_title = QLabel("Log Kejadian Anomali:")
        lbl_anomali_title.setStyleSheet("font-size: 9px; font-weight: bold; color: #ccc;")
        layout.addWidget(lbl_anomali_title)

        self.list_anomali = QListWidget()
        self.list_anomali.setStyleSheet("background-color: #ffffff; color: #222222; font-size: 8px;")
        self.list_anomali.addItem("Tidak ada kejadian anomali sepanjang sesi ini.")
        layout.addWidget(self.list_anomali, 2)

        self.lbl_session_summary = QLabel(
            "Sesi: 0 sample | RPM rata-rata: 0.0 | D² max: 0.00 | Kondisi terparah: Normal | Waspada: 0x, Bahaya: 0x."
        )
        self.lbl_session_summary.setStyleSheet(
            "font-size: 8px; color: #1c3d1c; background-color: #d7f0d7; padding: 3px; border-radius: 3px;"
        )
        self.lbl_session_summary.setWordWrap(True)
        layout.addWidget(self.lbl_session_summary)

        return page

