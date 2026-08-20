from PyQt5.QtWidgets import (
    QWidget, QLabel, QPushButton, QHBoxLayout, QVBoxLayout,
    QGridLayout, QStackedWidget, QFrame, QListWidget, QComboBox, QMessageBox,
    QSlider, QSizePolicy, QProgressBar, QLineEdit
)
from PyQt5.QtCore import QTimer, Qt, QPoint, QEvent
from PyQt5.QtGui import QFont, QPainter, QLinearGradient, QColor, QPolygon, QPen, QBrush
import pyqtgraph as pg
from config import COL_PANEL_DARK, COL_TEXT_LIGHT, COL_TEXT_DIM, COL_ACCENT, COL_OK


class RawPageMixin:
    """Method-method untuk halaman ini. Di-mixin ke class Dashboard
    (lihat dashboard_core.py) supaya semua method di sini bisa akses
    self.current_v, self.selected_slot_idx, dst -- gak ada state baru di sini,
    murni pemisahan biar dashboard_core.py gak sepanjang 2000 baris."""

    def _page_raw(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(1, 1, 1, 1)
        layout.setSpacing(1)

        main_raw = QHBoxLayout()
        main_raw.setSpacing(2)
        
        layout_grafik_grid = QGridLayout()
        layout_grafik_grid.setSpacing(2)
        layout_grafik_grid.setContentsMargins(0, 0, 0, 0)
        
        pg.setConfigOptions(antialias=True)

        def _tidy_plot(plot_widget, title):
            axis_font = QFont("Arial", 8)
            plot_item = plot_widget.getPlotItem()
            for axis_name in ("left", "bottom"):
                axis = plot_item.getAxis(axis_name)
                axis.setStyle(tickFont=axis_font, tickTextOffset=4, autoExpandTextSpace=True)
                axis.setTextPen(pg.mkPen('#dddddd'))
            plot_item.getAxis("bottom").setTickSpacing(major=15, minor=15)
            plot_item.getAxis("left").setWidth(35)
            plot_widget.setTitle(title, size="9pt")
            plot_widget.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        self.graph_a = pg.PlotWidget(title="Sound (dB)")
        self.graph_a.setBackground(COL_PANEL_DARK)
        self.graph_a.showGrid(x=True, y=True, alpha=0.2)
        self.curve_a = self.graph_a.plot(pen=pg.mkPen('#a78bfa', width=2))
        _tidy_plot(self.graph_a, "Sound (dB)")
        layout_grafik_grid.addWidget(self.graph_a, 0, 0)
        
        self.graph_temp = pg.PlotWidget(title="Temp (°C)")
        self.graph_temp.setBackground(COL_PANEL_DARK)
        self.graph_temp.showGrid(x=True, y=True, alpha=0.2)
        self.curve_temp = self.graph_temp.plot(pen=pg.mkPen('#f472b6', width=2))
        _tidy_plot(self.graph_temp, "Temp (°C)")
        layout_grafik_grid.addWidget(self.graph_temp, 0, 1)

        self.graph_v = pg.PlotWidget(title="Vibration (G)")
        self.graph_v.setBackground(COL_PANEL_DARK)
        self.graph_v.showGrid(x=True, y=True, alpha=0.2)
        self.curve_v = self.graph_v.plot(pen=pg.mkPen('#818cf8', width=2))
        _tidy_plot(self.graph_v, "Vibration (G)")
        layout_grafik_grid.addWidget(self.graph_v, 1, 0)
        
        self.panel_ai = QFrame()
        self.panel_ai.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border-radius: 4px;")
        lay_ai = QVBoxLayout(self.panel_ai)
        lay_ai.setContentsMargins(4, 2, 4, 2)
        lay_ai.setSpacing(2)

        lbl_ai_title = QLabel("🤖 ADVANCED DIAGNOSTICS & AI")
        lbl_ai_title.setStyleSheet(f"font-size: 8px; font-weight: bold; color: {COL_ACCENT};")
        lay_ai.addWidget(lbl_ai_title)

        grid_ai = QGridLayout()
        grid_ai.setHorizontalSpacing(4)
        grid_ai.setVerticalSpacing(1)

        def _ai_row(r, label_text):
            lbl1 = QLabel(label_text)
            lbl1.setStyleSheet("font-size: 9px; color: #999;")
            lbl2 = QLabel("--")
            lbl2.setStyleSheet("font-size: 8px; font-weight: bold; color: #eee;")
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
        self.prog_hs.setFixedHeight(5)
        self.prog_hs.setTextVisible(False)
        self.prog_hs.setStyleSheet(
            "QProgressBar { border: none; background-color: #333; border-radius: 2px; }"
            "QProgressBar::chunk { background-color: " + COL_OK + "; border-radius: 2px; }"
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

        panel_kanan = QVBoxLayout()
        panel_kanan.setSpacing(2)

        frame_status = QFrame()
        frame_status.setStyleSheet(f"background-color: {COL_PANEL_DARK}; border-radius: 4px;")
        fs_lay = QVBoxLayout(frame_status)
        fs_lay.setContentsMargins(4, 2, 4, 2)
        fs_lay.setSpacing(2)

        self.lbl_sys_status = QLabel("● STANDBY")
        self.lbl_sys_status.setStyleSheet("font-size: 8px; font-weight: bold; color: #888888; padding-bottom: 2px;")
        fs_lay.addWidget(self.lbl_sys_status)

        row_style = "border-bottom: 1px solid #33363b;"
        name_style = "font-size: 8px; color: #999999;"
        val_style = "font-size: 11px; color: #eeeeee; font-weight: bold;"

        grid_val = QGridLayout()
        grid_val.setContentsMargins(0, 0, 0, 0)
        grid_val.setHorizontalSpacing(4)
        grid_val.setVerticalSpacing(2)
        grid_val.setColumnStretch(0, 1)
        grid_val.setColumnStretch(1, 1)

        def _param_row(row, name):
            lbl_name = QLabel(name)
            lbl_name.setStyleSheet(name_style + row_style + "padding: 2px 0px;")
            lbl_val = QLabel("--")
            lbl_val.setStyleSheet(val_style + row_style + "padding: 2px 0px;")
            lbl_val.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
            grid_val.addWidget(lbl_name, row, 0)
            grid_val.addWidget(lbl_val, row, 1)
            return lbl_val

        self.lbl_val_v = _param_row(0, "Vibration")
        self.lbl_val_a = _param_row(1, "Sound")
        self.lbl_val_temp = _param_row(2, "Temp")
        self.lbl_val_rpm = _param_row(3, "RPM")
        self.lbl_val_d2 = _param_row(4, "D² (Severity)")

        fs_lay.addLayout(grid_val)

        self.lbl_val_vxyz = QLabel("(X: -- | Y: -- | Z: -- G)")
        self.lbl_val_vxyz.setStyleSheet("font-size: 8px; color: #777777; padding: 2px 0px 0px 0px;")
        self.lbl_val_vxyz.setWordWrap(True)
        fs_lay.addWidget(self.lbl_val_vxyz)

        panel_kanan.addWidget(frame_status)
        main_raw.addLayout(panel_kanan, 4)

        layout.addLayout(main_raw)
        return page

