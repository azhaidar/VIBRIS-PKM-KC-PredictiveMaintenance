from PyQt5.QtWidgets import QWidget, QVBoxLayout, QLabel
from PyQt5.QtCore import Qt, QPoint
from PyQt5.QtGui import QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush, QFont

from config import (
    COL_OK, COL_WARN, COL_BAD, COL_IDLE, COL_TEXT_LIGHT, COL_TEXT_DIM,
)

# ===================== KOMPONEN KUSTOM GAUGE SUMMARY =====================
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
        h = self.height() - 6

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
            QPoint(max(0, px - 6), h + 6),
            QPoint(min(w, px + 6), h + 6)
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
        layout.setSpacing(2)

        self.lbl_title = QLabel(title)
        self.lbl_title.setStyleSheet(f"font-size: 11px; font-weight: bold; color: {COL_TEXT_LIGHT};")
        layout.addWidget(self.lbl_title)

        self.bar = _GaugeBar(min_val, max_val)
        self.bar.setFixedHeight(22)
        layout.addWidget(self.bar)

        self.lbl_status = QLabel("Normal: No action required")
        self.lbl_status.setStyleSheet(f"font-size: 9px; color: {COL_TEXT_DIM};")
        self.lbl_status.setWordWrap(True)
        layout.addWidget(self.lbl_status)

    def set_value(self, val, status_key="normal"):
        if val is None: return
        self.current_val = val
        self.bar.set_value(val)

        # Prioritaskan status resmi dari device (hasil deteksi Mahalanobis D²,
        # yang menilai KOMBINASI semua parameter). Kalau cuma dinilai dari
        # nilai gauge ini sendiri (fallback di bawah), bisa terjadi gauge
        # tampil "Normal" hijau padahal status sistem keseluruhan "Bahaya" --
        # karena metode D2 justru menangkap pola gabungan yang gak kelihatan
        # kalau parameter dicek satu-satu.
        if status_key == "diam":
            status, desc = "DIAM / OFF", "Motor dalam keadaan mati"
            col = COL_IDLE
        elif status_key == "bahaya" or val >= self.t_danger:
            status, desc = "BAHAYA", "Segera periksa mesin"
            col = COL_BAD
        elif status_key == "waspada" or val >= self.t_warn:
            status, desc = "WASPADA", "Mulai naik, perlu dipantau"
            col = COL_WARN
        else:
            status, desc = "NORMAL", "Aman, tidak perlu tindakan"
            col = COL_OK

        self.lbl_status.setText(f"<span style='color:{col}; font-weight:bold;'>{status}:</span><br>{desc}")
