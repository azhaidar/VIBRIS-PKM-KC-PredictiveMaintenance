# Cara jalanin (Windows):
#   D:\projects\coding\env\Scripts\activate
#   python main.py
import sys

from PyQt5.QtWidgets import QApplication
from PyQt5.QtCore import Qt, QEvent

from dashboard_core import Dashboard

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