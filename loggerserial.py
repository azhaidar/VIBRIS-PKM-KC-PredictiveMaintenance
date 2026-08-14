"""
logger_serial_ke_excel.py

TUJUAN:
Membaca SEMUA baris yang dikirim ESP32-S3 lewat Serial (USB):
  1. Ditampilkan LANGSUNG di terminal, mirip Serial Monitor Arduino IDE,
     dengan warna berbeda per jenis baris (JSON, WARNING, FFT-DIAG, dst)
     supaya gampang dipantau mata sambil alat menyala.
  2. Baris yang berupa JSON data sensor (diawali karakter '{') SEKALIGUS
     ditulis sebagai baris baru ke file Excel (.xlsx) dan CSV cadangan,
     real-time, tanpa perlu copy-paste manual.

CARA PAKAI:
1. Install dependency (sekali saja):
       pip install pyserial openpyxl colorama
2. Tutup Arduino IDE Serial Monitor dulu (satu port cuma bisa dipakai
   satu program dalam satu waktu).
3. Cek nama port ESP32 kamu di Device Manager (Windows), update
   SERIAL_PORT di bawah kalau beda.
4. Jalankan: python loggerserial.py
5. Pilih nomor kondisi tes dari menu, atau 'X' buat reboot ESP32.
6. Ground-truth otomatis terkirim sesuai kondisi yang dipilih.
7. Ketik 1 huruf command + Enter kapan saja buat kirim command lain.
8. Tekan CTRL+C untuk berhenti.
"""

import serial
import json
import csv
import os
import time
import threading
from datetime import datetime
from colorama import init as colorama_init, Fore, Style

colorama_init(autoreset=True)

# ======================= KONFIGURASI =======================
SERIAL_PORT = "COM5"
BAUD_RATE = 115200

DAFTAR_KONDISI = {
    "1": ("kondisiNormal",       "O"),
    "2": ("kondisiUnbalance",    "U"),
    "3": ("kondisiMisalignment", "M"),
    "4": ("kondisiBearingFault", "F"),
    "5": ("kondisiLubrication",  "L"),
    "6": ("kondisiMati",         "D"),
}
# =============================================================

KOLOM = [
    "waktu_lokal", "rms_v", "rms_x", "rms_y", "rms_z", "rms_a",
    "cur", "cur_raw_adc", "temp", "temp_raw", "rpm", "snr", "severity",
    "status", "e_unbalance", "e_misalign", "e_bpfo", "e_bpfi",
    "diagnosis", "diag_conf",
    "e_audio_low", "e_audio_mid", "e_audio_high",
    "audio_diagnosis", "audio_diag_conf",
    "ml_label", "ml_conf",
    "roughness", "brightness",
    "health_score", "trend", "servis_estimasi",
    "ground_truth"
]


def warna_untuk(baris_teks):
    if baris_teks.startswith("[WARNING]"):
        return Fore.RED
    if "[FFT-DIAG]" in baris_teks:
        return Fore.CYAN
    if baris_teks.startswith("[FFT]"):
        return Fore.GREEN
    if baris_teks.startswith("="):
        return Fore.MAGENTA
    if baris_teks.startswith("{"):
        return Fore.WHITE + Style.DIM
    if baris_teks.startswith("[SYSTEM]") or baris_teks.startswith("[Calibrator]"):
        return Fore.YELLOW
    if "[AUDIO]" in baris_teks:
        return Fore.CYAN + Style.DIM
    return Fore.RESET



def siapkan_file_csv(path):
    file_baru = not os.path.exists(path)
    f = open(path, "a", newline="", encoding="utf-8")
    if file_baru:
        f.write("sep=,\n")
    writer = csv.writer(f)
    if file_baru:
        writer.writerow(KOLOM)
    return f, writer


def listener_command(ser):
    print("[LOGGER] Ketik 1 huruf command (O/U/M/F/L/D/R/0-9/dst) lalu Enter, kapan saja.")
    while True:
        try:
            cmd = input().strip()
            if cmd and ser.is_open:
                ser.write(cmd[0].encode())
                print(f"[LOGGER] Command '{cmd[0]}' terkirim ke ESP32.")
        except (EOFError, KeyboardInterrupt):
            break


def main():
    while True:
        print("\nPilih kondisi tes:")
        for nomor, (nama, _) in DAFTAR_KONDISI.items():
            print(f"  {nomor}. {nama}")
        print("  X. Reboot ESP32 (tanpa mulai rekam data)")
        print("  R. Kalibrasi ulang (tanpa reboot, koneksi tetap nyala)")
        print("  Q. Keluar dari program")

        pilihan = input("Nomor pilihan: ").strip()

        if pilihan.upper() == "Q":
            print("[LOGGER] Keluar.")
            return

        if pilihan.upper() == "X":
            try:
                ser_temp = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
                ser_temp.write(b'X')
                ser_temp.close()
                print("[LOGGER] Reboot terkirim. Menunggu 3 detik...")
                time.sleep(3)
            except serial.SerialException as e:
                print(f"[LOGGER] GAGAL kirim reboot: {e}")
            continue

        if pilihan.upper() == "R":
            try:
                ser_temp = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
                ser_temp.write(b'R')
                ser_temp.close()
                print("[LOGGER] Kalibrasi ulang terkirim. Device tetap nyala, langsung mulai belajar baseline baru.")
            except serial.SerialException as e:
                print(f"[LOGGER] GAGAL kirim kalibrasi ulang: {e}")
            continue

        if pilihan not in DAFTAR_KONDISI:
            print(f"[LOGGER] Nomor '{pilihan}' tidak dikenal, pakai default 'tanpaNama'.")
            kondisi, cmd = "tanpaNama", None
        else:
            kondisi, cmd = DAFTAR_KONDISI[pilihan]

        timestamp = datetime.now().strftime("%Y%m%d_%H%M")
        output_csv  = f"vibris_{timestamp}_{kondisi}.csv"

        print(f"[LOGGER] Menyambungkan ke {SERIAL_PORT} @ {BAUD_RATE} baud...")
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
        except serial.SerialException as e:
            print(f"[LOGGER] GAGAL membuka {SERIAL_PORT}: {e}")
            print("[LOGGER] Cek: nama port benar? Serial Monitor sudah ditutup? Kabel USB tersambung?")
            return

        print("[LOGGER] Tersambung.")

        if cmd:
            ser.write(cmd.encode())
            print(f"[LOGGER] Ground-truth '{cmd}' ({kondisi}) terkirim. Mulai logging dalam 2 detik...")
            time.sleep(2)

        threading.Thread(target=listener_command, args=(ser,), daemon=True).start()

        print("[LOGGER] Menunggu data dari ESP32...\n")

        csv_file, csv_writer = siapkan_file_csv(output_csv)

        baris_masuk = 0
        try:
            while True:
                raw_line = ser.readline().decode("utf-8", errors="ignore").strip()
                if not raw_line:
                    continue

                warna = warna_untuk(raw_line)
                print(f"{warna}{raw_line}{Style.RESET_ALL}")

                if not raw_line.startswith("{"):
                    continue

                try:
                    data = json.loads(raw_line)
                except json.JSONDecodeError:
                    print(f"{Fore.RED}[LOGGER] Baris JSON rusak, dilewati.{Style.RESET_ALL}")
                    continue

                waktu = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                baris = [
                    waktu,
                    data.get("rms_v"), data.get("rms_x"), data.get("rms_y"), data.get("rms_z"),
                    data.get("rms_a"), data.get("cur"), data.get("cur_raw_adc"),
                    data.get("temp"), data.get("temp_raw"), data.get("rpm"), data.get("snr"),
                    data.get("severity"), data.get("status"),
                    data.get("e_unbalance"), data.get("e_misalign"),
                    data.get("e_bpfo"), data.get("e_bpfi"),
                    data.get("diagnosis"), data.get("diag_conf"),
                    data.get("e_audio_low"), data.get("e_audio_mid"), data.get("e_audio_high"),
                    data.get("audio_diagnosis"), data.get("audio_diag_conf"),
                    data.get("ml_label"), data.get("ml_conf"),
                    data.get("roughness"), data.get("brightness"),
                    data.get("health_score"), data.get("trend"), data.get("servis_estimasi"),
                    data.get("ground_truth"),
                ]

                csv_writer.writerow(baris)
                csv_file.flush()
                baris_masuk += 1

                print(f"{Fore.YELLOW}[LOGGER] Baris #{baris_masuk} tersimpan | "
                    f"status={data.get('status')} rpm={data.get('rpm')}{Style.RESET_ALL}")

        except KeyboardInterrupt:
            print("\n[LOGGER] Dihentikan oleh user. Menyimpan file terakhir...")
        except serial.SerialException as e:
            print(f"[LOGGER] ERROR koneksi serial: {e}")
        finally:
            csv_file.close()
            ser.close()
            print(f"[LOGGER] Selesai. Total {baris_masuk} baris tersimpan di {output_csv}")


if __name__ == "__main__":
    main()