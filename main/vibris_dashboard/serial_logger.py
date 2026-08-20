"""
serial_logger.py

Script BERDIRI SENDIRI (gak nyambung ke dashboard_core.py atau apapun) --
satu-satunya tugasnya: buka port serial yang sama dipakai ESP32, terus
tulis SEMUA baris yang lewat (termasuk teks biasa seperti
"[SYSTEM] Kalibrasi VALID" dan "[EXPORT PRESET]") ke 1 file teks di folder
ini, sambil juga nampilin ke layar biar kamu bisa lihat progress-nya.

KENAPA INI PERLU ADA:
dashboard_core.py (lewat serial_worker.py) CUMA baca baris yang berbentuk
JSON (diawali '{') -- semua teks biasa dari firmware dibuang. Serial
Monitor Arduino IDE bisa lihat semuanya, tapi nyalin teks panjang dari
situ ribet. Script ini jalan tengah: baca SEMUA baris (gak filter apa-apa),
simpen ke file biasa yang gampang dibuka/dibaca ulang.

CARA PAKAI:
1. TUTUP Arduino IDE Serial Monitor kalau lagi kebuka (port cuma bisa
   dipakai 1 program).
2. Jalankan dari terminal/command prompt, di folder ini:
       python serial_logger.py
   (kalau ada error "No module named serial", jalankan dulu:
       pip install pyserial
    lalu ulangi.)
3. Script bakal coba nebak port COM sendiri (nyari device USB-serial
   kayak CH340/CP210x/dst, sama seperti dashboard). Kalau gagal nebak,
   kamu akan diminta ketik nama port manual (contoh: COM3 -- itu port
   yang kelihatan di log upload Arduino IDE kamu barusan).
4. Biarkan jalan, lakuin kalibrasi seperti biasa (via Serial Monitor
   Arduino IDE ATAU langsung ketik command di sini -- script ini juga
   bisa kirim command, ketik 1 karakter lalu Enter).
5. Setelah selesai (muncul teks "[EXPORT PRESET] Selesai."), tekan
   Ctrl+C buat berhenti. File "serial_log.txt" di folder ini udah berisi
   semua yang lewat -- kasih tau, biar dibaca langsung dari sana.
"""

import sys
import time
import glob

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Modul 'pyserial' belum terpasang. Jalankan dulu:")
    print("    pip install pyserial")
    sys.exit(1)

BAUD_RATE = 115200
LOG_FILE = "serial_log.txt"

# Sama persis daftar petunjuk USB yang dipakai config.py dashboard, biar
# konsisten nebak port yang sama.
ESP32_USB_HINTS = ["CH340", "CH343", "CP210", "USB-SERIAL", "USB SERIAL", "FTDI", "SILICON LABS"]


def guess_port():
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").upper()
        if any(hint in desc for hint in ESP32_USB_HINTS):
            return p.device
    return None


def main():
    port = guess_port()
    if port:
        print(f"[serial_logger] Ketemu port kemungkinan ESP32: {port}")
        confirm = input(f"Pakai port ini? (Enter buat YA, atau ketik nama port lain): ").strip()
        if confirm:
            port = confirm
    else:
        print("[serial_logger] Gak berhasil nebak port otomatis.")
        port = input("Ketik nama port serial ESP32 (contoh: COM3): ").strip()

    if not port:
        print("[serial_logger] Gak ada port dipilih, berhenti.")
        return

    print(f"[serial_logger] Membuka {port} @ {BAUD_RATE}...")
    ser = serial.Serial(port, BAUD_RATE, timeout=1)
    time.sleep(2)  # kasih waktu ESP32 selesai reset setelah port dibuka

    print(f"[serial_logger] Tersambung. Semua baris ditulis juga ke '{LOG_FILE}'.")
    print("[serial_logger] Ketik 1 karakter command lalu Enter buat kirim ke ESP32 (opsional).")
    print("[serial_logger] Tekan Ctrl+C buat berhenti.\n")

    import threading

    def read_loop(f):
        while True:
            try:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode('utf-8', errors='ignore').rstrip('\r\n')
                if line == "":
                    continue
                stamped = f"[{time.strftime('%H:%M:%S')}] {line}"
                print(stamped)
                f.write(stamped + "\n")
                f.flush()
            except serial.SerialException:
                print("[serial_logger] Koneksi serial terputus.")
                return

    with open(LOG_FILE, "a", encoding="utf-8") as f:
        f.write(f"\n===== SESI BARU {time.strftime('%Y-%m-%d %H:%M:%S')} =====\n")
        reader = threading.Thread(target=read_loop, args=(f,), daemon=True)
        reader.start()
        try:
            while True:
                cmd = input()
                if cmd:
                    ser.write(cmd[0].encode('utf-8'))
                    print(f"[serial_logger] Kirim command: '{cmd[0]}'")
        except KeyboardInterrupt:
            print("\n[serial_logger] Berhenti.")


if __name__ == "__main__":
    main()
