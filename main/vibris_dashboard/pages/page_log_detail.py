from PyQt5.QtWidgets import (
    QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush
import pyqtgraph as pg
import csv
from config import COL_PANEL_DARK, COL_TEXT_DIM, COL_BAD, COL_WARN, COL_OK


class LogDetailMixin:
    """Method-method untuk halaman ini. Di-mixin ke class Dashboard
    (lihat dashboard_core.py) supaya semua method di sini bisa akses
    self.current_v, self.selected_slot_idx, dst -- gak ada state baru di sini,
    murni pemisahan biar dashboard_core.py gak sepanjang 2000 baris."""

    def _page_log_detail(self):
        page = QWidget()
        root = QVBoxLayout(page)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        self.lbl_logdet_title = QLabel("HASIL DETEKSI REKAMAN: -")
        self.lbl_logdet_title.setStyleSheet("font-size: 9px; font-weight: bold; color: #ffffff;")
        self.lbl_logdet_title.setWordWrap(True)
        root.addWidget(self.lbl_logdet_title)

        status_row = QHBoxLayout()
        status_row.setSpacing(4)

        self.lbl_logdet_dot = QLabel("●")
        self.lbl_logdet_dot.setStyleSheet("font-size: 14px; color: #888888;")
        status_row.addWidget(self.lbl_logdet_dot)

        self.lbl_logdet_peak = QLabel("Nilai puncak: -")
        self.lbl_logdet_peak.setWordWrap(True)
        self.lbl_logdet_peak.setStyleSheet("font-size: 8px; color: #cccccc;")
        status_row.addWidget(self.lbl_logdet_peak, 1)

        root.addLayout(status_row)

        grid = QGridLayout()
        grid.setSpacing(2)
        pg.setConfigOptions(antialias=True)

        self.graph_logdet_a = pg.PlotWidget(title="Sound (dB)")
        self.graph_logdet_a.setBackground(COL_PANEL_DARK)
        self.graph_logdet_a.showGrid(x=True, y=True, alpha=0.2)
        self.curve_logdet_a = self.graph_logdet_a.plot(pen=pg.mkPen('#4da6ff', width=1.5))
        grid.addWidget(self.graph_logdet_a, 0, 0)

        self.graph_logdet_temp = pg.PlotWidget(title="Temp (°C)")
        self.graph_logdet_temp.setBackground(COL_PANEL_DARK)
        self.graph_logdet_temp.showGrid(x=True, y=True, alpha=0.2)
        self.curve_logdet_temp = self.graph_logdet_temp.plot(pen=pg.mkPen('#e040fb', width=1.5))
        grid.addWidget(self.graph_logdet_temp, 0, 1)

        self.graph_logdet_v = pg.PlotWidget(title="Vibration (G)")
        self.graph_logdet_v.setBackground(COL_PANEL_DARK)
        self.graph_logdet_v.showGrid(x=True, y=True, alpha=0.2)
        self.curve_logdet_v = self.graph_logdet_v.plot(pen=pg.mkPen('#ff4d4d', width=1.5))
        grid.addWidget(self.graph_logdet_v, 1, 0)

        self.reserved_slot_logdet = QFrame()
        self.reserved_slot_logdet.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border: 1px dashed {COL_TEXT_DIM}; border-radius: 4px;")
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

        self.btn_logdet_play_pause.setStyleSheet("background-color: #cfcfcf; color: #000000; font-weight: bold; font-size: 8px; height: 24px; border-radius: 4px;")
        self.btn_logdet_back.setStyleSheet(f"background-color: {COL_BAD}; color: #ffffff; font-weight: bold; font-size: 8px; height: 24px; border-radius: 4px;")
        self.speed_logdet_combo.setStyleSheet(f"background-color: {COL_PANEL_DARK}; color: white; font-size: 8px;")

        self.btn_logdet_play_pause.clicked.connect(self._logdet_toggle_play)
        self.speed_logdet_combo.currentIndexChanged.connect(self._logdet_change_speed)
        self.btn_logdet_back.clicked.connect(self._logdet_back)

        self.slider_logdet = QSlider(Qt.Horizontal)
        self.slider_logdet.setMinimum(0)
        self.slider_logdet.setMaximum(0)
        self.slider_logdet.sliderMoved.connect(self._logdet_seek)

        self.lbl_logdet_pos = QLabel("0 / 0")
        self.lbl_logdet_pos.setStyleSheet("font-size: 9px; color: #aaa;")

        ctrl.addWidget(self.btn_logdet_play_pause, 2)
        ctrl.addWidget(self.speed_logdet_combo, 1)
        ctrl.addWidget(self.slider_logdet, 5)
        ctrl.addWidget(self.lbl_logdet_pos, 1)
        ctrl.addWidget(self.btn_logdet_back, 2)
        root.addLayout(ctrl)

        return page

    # ===================== HALAMAN MODE AWAM =====================
    # Tiga halaman ini gak nambah pipa data baru -- semuanya baca ulang
    # variabel yang sama yang sudah diisi oleh _read_serial_worker
    # (current_health_score, current_diag_label, session_waspada_count, dst).
    # Bedanya cuma cara nampilinnya: bahasa awam, tanpa istilah teknis.


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
            QMessageBox.critical(self, "Error", f"Gagal membaca rekaman: {e}")
        return times, v_vals, a_vals, t_vals


    def _logdet_render_diagnosis_summary(self):
        if not self.logdet_v_vals:
            self.lbl_logdet_dot.setStyleSheet("font-size: 14px; color: #888888;")
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

        self.lbl_logdet_dot.setStyleSheet(f"font-size: 14px; color: {col};")
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
    
