"""
loggerserial.py

TUJUAN:
Membaca SEMUA baris yang dikirim ESP32-S3 lewat Serial (USB):
  1. Ditampilkan LANGSUNG di terminal, warna beda per jenis baris.
  2. Baris JSON data sensor (diawali '{') otomatis ditulis ke CSV, real-time.
  3. Baris JSON ringkasan sesi cek ("type":"session_summary") DIPISAH --
     tidak masuk CSV data mentah, tapi dicetak besar + disimpan ke file
     .txt terpisah.
  4. Interaksi lewat MENU BERTINGKAT gaya USSD (pilih kategori dulu, baru
     sub-menu, ada 'B' buat Kembali di tiap level) -- supaya tidak ada lagi
     angka yang dobel arti (dulu: '1' bisa berarti kondisi ATAU slot,
     sekarang dipisah total, tidak mungkin ketuker).
  5. Data tetap ngalir di background sambil menu dibuka, berkat 2 thread
     terpisah -- satu baca data masuk, satu urus menu/input kamu.

CARA PAKAI:
1. pip install pyserial colorama
2. Tutup Arduino IDE Serial Monitor dulu.
3. Ganti SERIAL_PORT di bawah sesuai port ESP32 kamu.
4. Jalankan: python loggerserial.py
5. Ketik M kapan saja buat buka menu, navigasi pakai angka, B buat kembali.
6. CTRL+C untuk berhenti paksa, atau Q di menu utama untuk keluar rapi.
"""

import serial
import json
import csv
import os
import threading
import time
from datetime import datetime
from colorama import init as colorama_init, Fore, Style

colorama_init(autoreset=True)

# ======================= KONFIGURASI =======================
SERIAL_PORT = "COM3"      # GANTI sesuai port ESP32 kamu
BAUD_RATE = 115200
REKAM_DURASI_MENIT = 5    # BARU: auto-stop flat, ganti angka ini kalau mau beda
DATASET_DIR = "Dataset"   # BARU: semua file CSV/txt hasil logging disimpan di sini
SYSTEM_LOG_FILE = os.path.join(DATASET_DIR, "vibris_system_log.txt")   # BARU: nampung semua baris BUKAN JSON, biar gak hilang
_timestamp = datetime.now().strftime("%Y%m%d_%H%M")
_kondisi_aktif = "belumDipilih"
menu_terbuka = threading.Event()   # BARU: kalau nyala, print data JSON DIBUNGKAM sementara


def _bangun_nama_file():
    global OUTPUT_CSV, SUMMARY_LOG_FILE
    os.makedirs(DATASET_DIR, exist_ok=True)   # BARU: bikin folder Dataset kalau belum ada
    OUTPUT_CSV = os.path.join(DATASET_DIR, f"vibris_{_timestamp}_{_kondisi_aktif}.csv")
    SUMMARY_LOG_FILE = os.path.join(DATASET_DIR, f"vibris_{_timestamp}_{_kondisi_aktif}_ringkasan_sesi.txt")


_bangun_nama_file()

KOLOM = [
    "waktu_lokal", "rms_v", "rms_x", "rms_y", "rms_z", "rms_a",
    "cur", "cur_raw_adc", "temp", "temp_raw", "rpm", "snr", "d2",
    "status", "e_unbalance", "e_misalign", "e_bpfo", "e_bpfi",
    "diagnosis", "diag_conf",
    "e_audio_low", "e_audio_mid", "e_audio_high",
    "audio_diagnosis", "audio_diag_conf",
    "ml_label", "ml_conf",
    "roughness", "brightness",
    "ground_truth"
]

# ===================================================================
# LAPIS 1 -- KOMUNIKASI KE ESP32 (murni logic, dipakai ulang di semua menu)
# ===================================================================

def kirim_command(ser, huruf):
    ser.write(huruf.encode())
    print(f"{Fore.CYAN}[LOGGER] Command '{huruf}' terkirim ke ESP32.{Style.RESET_ALL}")


def set_kondisi(ser, kode_menu):
    huruf, nama_kondisi = MENU_KONDISI[kode_menu]
    kirim_command(ser, huruf)
    global _kondisi_aktif
    _kondisi_aktif = nama_kondisi
    _bangun_nama_file()
    print(f"{Fore.GREEN}[LOGGER] Kondisi diset: {nama_kondisi}. "
          f"File akan disimpan sebagai {OUTPUT_CSV}{Style.RESET_ALL}")


def pilih_slot(ser, state, slot):
    kirim_command(ser, str(slot))
    state["slot_aktif"] = slot
    print(f"{Fore.GREEN}[LOGGER] Slot motor aktif sekarang: #{slot}{Style.RESET_ALL}")


def pilih_regime(ser, state, regime):
    # BARU: regime 0-9 dikirim sebagai huruf kecil 'a'-'j' ke firmware
    # (lihat main.ino: cmd >= 'a' && cmd <= 'j' -> selectRegime(cmd - 'a')).
    # Ini SUMBU TERPISAH dari slot -- slot = mesin yang mana, regime =
    # kondisi operasi yang mana (misal pulley kecil/besar) PADA mesin itu.
    huruf = chr(ord('a') + regime)
    kirim_command(ser, huruf)
    state["regime_aktif"] = regime
    print(f"{Fore.GREEN}[LOGGER] Regime/kondisi operasi aktif sekarang: #{regime}{Style.RESET_ALL}")


def hapus_kalibrasi(ser, slot_aktif):
    konfirmasi = input(
        f"{Fore.RED}Yakin mau HAPUS baseline slot #{slot_aktif}? "
        f"Ini tidak bisa dibatalkan. Ketik 'HAPUS' untuk konfirmasi: {Style.RESET_ALL}"
    )
    if konfirmasi.strip().upper() == "HAPUS":
        kirim_command(ser, "Z")
    else:
        print(f"{Fore.YELLOW}[LOGGER] Dibatalkan, baseline TIDAK dihapus.{Style.RESET_ALL}")


# ===================================================================
# LAPIS 2 -- DAFTAR MENU (data doang)
# ===================================================================

MENU_KONDISI = {
    "1": ("O", "kondisiNormal"),
    "2": ("U", "kondisiUnbalance"),
    "3": ("M", "kondisiMisalignment"),
    "4": ("F", "kondisiBearingFaulting"),
    "5": ("L", "kondisiLubrication"),
    "6": ("D", "kondisiMati"),
}

# ===================================================================
# LAPIS 3 -- MENU BERTINGKAT (setiap fungsi = 1 "layar", loop sampai
# user pilih B untuk kembali, atau selesai 1 aksi)
# ===================================================================

def submenu_kondisi(ser, state):
    while True:
        print(f"\n{Fore.CYAN}--- KONDISI MOTOR (slot aktif: #{state['slot_aktif']}) ---{Style.RESET_ALL}")
        for k, (_, nama) in MENU_KONDISI.items():
            print(f"  {k}. {nama}")
        print("  B. Kembali ke menu utama")

        pilihan = input("Pilih: ").strip().upper()
        if pilihan == "B":
            return
        if pilihan in MENU_KONDISI:
            set_kondisi(ser, pilihan)
            return
        print(f"{Fore.RED}[LOGGER] Pilihan tidak dikenal, coba lagi.{Style.RESET_ALL}")


def submenu_slot(ser, state):
    while True:
        print(f"\n{Fore.CYAN}--- SLOT MOTOR (aktif sekarang: #{state['slot_aktif']}) ---{Style.RESET_ALL}")
        print("  Ketik angka 0-9 untuk pilih slot motor")
        print("  B. Kembali ke menu utama")

        pilihan = input("Pilih: ").strip().upper()
        if pilihan == "B":
            return
        if pilihan.isdigit() and 0 <= int(pilihan) <= 9:
            pilih_slot(ser, state, int(pilihan))
            return
        print(f"{Fore.RED}[LOGGER] Harus angka 0-9, coba lagi.{Style.RESET_ALL}")


def submenu_regime(ser, state):
    while True:
        print(f"\n{Fore.CYAN}--- REGIME / KONDISI OPERASI (slot #{state['slot_aktif']}, "
              f"regime aktif sekarang: #{state['regime_aktif']}) ---{Style.RESET_ALL}")
        print("  Ketik angka 0-9 untuk pilih regime (contoh: 0=pulley besar/default, 1=pulley kecil, dst --")
        print("  urutannya terserah kamu, yang penting KONSISTEN tiap kali pakai regime yang sama)")
        print("  B. Kembali ke menu utama")

        pilihan = input("Pilih: ").strip().upper()
        if pilihan == "B":
            return
        if pilihan.isdigit() and 0 <= int(pilihan) <= 9:
            pilih_regime(ser, state, int(pilihan))
            return
        print(f"{Fore.RED}[LOGGER] Harus angka 0-9, coba lagi.{Style.RESET_ALL}")


def submenu_sesi_cek(ser, state):
    while True:
        print(f"\n{Fore.CYAN}--- SESI CEK & RIWAYAT (slot aktif: #{state['slot_aktif']}) ---{Style.RESET_ALL}")
        print("  1. Mulai sesi CEK 1 menit")
        print("  2. Tampilkan hasil cek TERAKHIR slot ini")
        print("  B. Kembali ke menu utama")

        pilihan = input("Pilih: ").strip().upper()
        if pilihan == "B":
            return
        if pilihan == "1":
            kirim_command(ser, "K")
            print(f"{Fore.YELLOW}[LOGGER] Sesi cek dimulai, tunggu ~1 menit untuk ringkasan...{Style.RESET_ALL}")
            return
        if pilihan == "2":
            kirim_command(ser, "P")
            return
        print(f"{Fore.RED}[LOGGER] Pilihan tidak dikenal, coba lagi.{Style.RESET_ALL}")


def submenu_kalibrasi(ser, state):
    while True:
        print(f"\n{Fore.CYAN}--- KALIBRASI (slot aktif: #{state['slot_aktif']}, "
              f"regime aktif: #{state['regime_aktif']}) ---{Style.RESET_ALL}")
        print("  1. Kalibrasi ULANG (timpa baseline slot ini, 3 menit)")
        print("  2. HAPUS baseline slot ini (butuh konfirmasi ketik)")
        print("  B. Kembali ke menu utama")

        pilihan = input("Pilih: ").strip().upper()
        if pilihan == "B":
            return
        if pilihan == "1":
            kirim_command(ser, "R")
            print(f"{Fore.YELLOW}[LOGGER] Kalibrasi ulang dimulai, tunggu 3 menit...{Style.RESET_ALL}")
            return
        if pilihan == "2":
            hapus_kalibrasi(ser, state["slot_aktif"])
            return
        print(f"{Fore.RED}[LOGGER] Pilihan tidak dikenal, coba lagi.{Style.RESET_ALL}")


def submenu_sistem(ser, state):
    while True:
        print(f"\n{Fore.CYAN}--- SISTEM ---{Style.RESET_ALL}")
        print("  1. Reboot ESP32 penuh")
        print("  B. Kembali ke menu utama")

        pilihan = input("Pilih: ").strip().upper()
        if pilihan == "B":
            return
        if pilihan == "1":
            konfirmasi = input(f"{Fore.RED}Yakin reboot ESP32? Koneksi akan terputus sesaat. "
                                f"Ketik 'YA': {Style.RESET_ALL}")
            if konfirmasi.strip().upper() == "YA":
                kirim_command(ser, "X")
            else:
                print(f"{Fore.YELLOW}[LOGGER] Dibatalkan.{Style.RESET_ALL}")
            return
        print(f"{Fore.RED}[LOGGER] Pilihan tidak dikenal, coba lagi.{Style.RESET_ALL}")


def menu_utama(ser, state):
    """Layar paling atas. Terus tampil lagi setelah tiap submenu selesai,
    SAMPAI user eksplisit pilih '0' (lanjut streaming) atau 'Q' (keluar
    program). Return 'KELUAR' atau 'LANJUT_STREAMING'."""
    while True:
        print(f"\n{Fore.MAGENTA}{Style.BRIGHT}=== MENU UTAMA ==="
              f"{Style.RESET_ALL} {Fore.WHITE}(slot aktif: #{state['slot_aktif']}, "
              f"regime: #{state['regime_aktif']}, "
              f"kondisi: {_kondisi_aktif}){Style.RESET_ALL}")
        print("  1. Kondisi Motor")
        print("  2. Slot Motor")
        print("  3. Sesi Cek & Riwayat")
        print("  4. Kalibrasi")
        print("  5. Sistem")
        print("  6. Regime / Kondisi Operasi (BARU -- pulley kecil/besar, dst)")
        print("  0. Selesai, lanjut memantau data")
        print("  Q. Keluar dari program")

        pilihan = input("Pilih: ").strip().upper()
        if pilihan == "1":
            submenu_kondisi(ser, state)
        elif pilihan == "2":
            submenu_slot(ser, state)
        elif pilihan == "3":
            submenu_sesi_cek(ser, state)
        elif pilihan == "4":
            submenu_kalibrasi(ser, state)
        elif pilihan == "5":
            submenu_sistem(ser, state)
        elif pilihan == "6":
            submenu_regime(ser, state)
        elif pilihan == "0":
            return "LANJUT_STREAMING"
        elif pilihan == "Q":
            return "KELUAR"
        else:
            print(f"{Fore.RED}[LOGGER] Pilihan tidak dikenal, coba lagi.{Style.RESET_ALL}")
        # Setelah submenu manapun selesai (baik user pilih aksi ATAU pilih
        # B buat batal), loop 'while True' ini BALIK KE ATAS lagi,
        # nampilin menu utama lagi -- BUKAN otomatis keluar/streaming.


# ===================================================================
# TAMPILAN & PENYIMPANAN DATA MENTAH
# ===================================================================

def warna_untuk(baris_teks):
    if '"type":"session_summary"' in baris_teks:
        return Fore.GREEN + Style.BRIGHT
    if baris_teks.startswith("[WARNING]"):
        return Fore.RED
    if baris_teks.startswith("[ERROR]"):
        return Fore.RED + Style.BRIGHT
    if "[FFT-DIAG]" in baris_teks:
        return Fore.CYAN
    if baris_teks.startswith("[FFT]"):
        return Fore.GREEN
    if baris_teks.startswith("="):
        return Fore.MAGENTA
    if baris_teks.startswith("{"):
        return Fore.WHITE + Style.DIM
    if baris_teks.startswith("[SYSTEM]") or baris_teks.startswith("[Calibrator]") \
            or baris_teks.startswith("[CheckSession]") or baris_teks.startswith("[CMD]") \
            or baris_teks.startswith("[TEST]"):
        return Fore.YELLOW
    if "[AUDIO]" in baris_teks:
        return Fore.CYAN + Style.DIM
    return Fore.RESET


def siapkan_file_csv(path):
    file_baru = not os.path.exists(path)
    f = open(path, "a", newline="", encoding="utf-8")
    writer = csv.writer(f)
    if file_baru:
        writer.writerow(KOLOM)
    return f, writer


def tulis_ringkasan_sesi(data, cetak=True):
    waktu = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    teks = (
        f"\n{'=' * 60}\n"
        f"[RINGKASAN SESI CEK] {waktu}\n"
        f"  Slot mesin          : #{data.get('slot')}\n"
        f"  Kesimpulan dominan  : {data.get('dominant')}\n"
        f"  Durasi sesi         : {data.get('duration_ms')} ms "
        f"({data.get('total_samples')} sample)\n"
        f"  Normal={data.get('n_normal')}  Waspada={data.get('n_waspada')}  "
        f"Bahaya={data.get('n_bahaya')}  Diam={data.get('n_diam')}\n"
        f"  Diagnosis terdeteksi: Unbalance={data.get('n_unbalance')} "
        f"Misalign={data.get('n_misalign')} BPFO={data.get('n_bpfo')} "
        f"BPFI={data.get('n_bpfi')}\n"
        f"  Rata-rata Health Score : {data.get('avg_health')}\n"
        f"  Suhu awal -> akhir sesi: {data.get('temp_start')}C -> "
        f"{data.get('temp_end')}C (delta {data.get('temp_delta')}C)\n"
        f"{'=' * 60}\n"
    )
    if cetak:
        print(f"{Fore.GREEN}{Style.BRIGHT}{teks}{Style.RESET_ALL}")
    with open(SUMMARY_LOG_FILE, "a", encoding="utf-8") as f:
        f.write(teks)


# ===================================================================
# THREAD -- urus menu, KAPAN SAJA, tanpa nge-block pembacaan data serial
# ===================================================================

def thread_menu(ser, state, stop_event):
    while not stop_event.is_set():
        try:
            teks = input().strip().upper()
        except EOFError:
            break
        if teks == "Q":
            stop_event.set()
            break
        if teks == "M":
            menu_terbuka.set()   # BARU: bungkam print data selama menu kebuka
            hasil = menu_utama(ser, state)
            menu_terbuka.clear()   # BARU: nyalain lagi print data
            if hasil == "KELUAR":
                stop_event.set()
                break
            print(f"{Fore.CYAN}[LOGGER] Kembali memantau data. "
                  f"Ketik M lagi kapan saja untuk buka menu.{Style.RESET_ALL}")
        else:
            print(f"{Fore.YELLOW}[LOGGER] Ketik 'M' untuk buka menu, atau 'Q' untuk keluar.{Style.RESET_ALL}")


# ===================================================================
# MAIN
# ===================================================================

def main():
    print(f"[LOGGER] Menyambungkan ke {SERIAL_PORT} @ {BAUD_RATE} baud...")
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
    except serial.SerialException as e:
        print(f"[LOGGER] GAGAL membuka {SERIAL_PORT}: {e}")
        print("[LOGGER] Cek: nama port benar? Arduino Serial Monitor sudah ditutup? "
              "Kabel USB tersambung?")
        return

    print(f"{Fore.GREEN}[LOGGER] Tersambung.{Style.RESET_ALL}")

    state = {"slot_aktif": 0, "regime_aktif": 0}

    # BARU: data TIDAK mulai mengalir otomatis. User WAJIB ketik S dulu
    # untuk mulai streaming+logging. Sebelum itu, bisa buka menu (M) dulu
    # untuk atur slot/kondisi dengan tenang, layar masih kosong/sepi.
    while True:
        print(f"\n{Fore.CYAN}Belum mulai logging. Ketik salah satu:{Style.RESET_ALL}")
        print("  S - Mulai streaming & logging data")
        print("  M - Buka menu dulu (atur slot/kondisi/kalibrasi, dst)")
        print("  Q - Keluar tanpa mulai")
        pilihan = input("Pilih: ").strip().upper()
        if pilihan == "S":
            break
        if pilihan == "M":
            hasil = menu_utama(ser, state)
            if hasil == "KELUAR":
                ser.close()
                print("[LOGGER] Keluar sebelum mulai logging.")
                return
            if hasil == "LANJUT_STREAMING":
                break   # BARU: user ketik 0 di dalam menu = sama kayak ketik S
            # setelah selesai 1 aksi di menu, balik ke prompt S/M/Q ini lagi
            continue
        if pilihan == "Q":
            ser.close()
            print("[LOGGER] Keluar tanpa mulai logging.")
            return
        print(f"{Fore.RED}[LOGGER] Ketik S, M, atau Q.{Style.RESET_ALL}")

    print(f"{Fore.CYAN}{Style.BRIGHT}[LOGGER] Mulai logging. Data akan mengalir di bawah ini. "
          f"Ketik M lalu Enter KAPAN SAJA untuk buka menu, atau Q untuk keluar.{Style.RESET_ALL}\n")

    # BARU: auto-stop FLAT 5 menit, tidak perlu input user -- ubah angka
    # REKAM_DURASI_MENIT ini di bagian atas file kalau mau beda dari 5 menit.
    sesi_mulai_ts = time.time()
    batas_detik = REKAM_DURASI_MENIT * 60
    print(f"{Fore.GREEN}[LOGGER] Auto-stop aktif: {REKAM_DURASI_MENIT} menit "
          f"({int(batas_detik)} detik).{Style.RESET_ALL}")

    csv_file, csv_writer = siapkan_file_csv(OUTPUT_CSV)
    current_csv_path = OUTPUT_CSV   # BARU: buat deteksi kalau nama file berubah

    stop_event = threading.Event()
    t_menu = threading.Thread(target=thread_menu, args=(ser, state, stop_event), daemon=True)
    t_menu.start()

    baris_masuk = 0
    try:
        while not stop_event.is_set():
            # BARU: cek batas waktu tiap iterasi, sebelum baca data
            if (time.time() - sesi_mulai_ts) >= batas_detik:
                print(f"\n{Fore.GREEN}{Style.BRIGHT}[LOGGER] Waktu {REKAM_DURASI_MENIT} menit tercapai, "
                      f"berhenti otomatis...{Style.RESET_ALL}")
                stop_event.set()
                break

            raw_line = ser.readline().decode("utf-8", errors="ignore").strip()

            if not raw_line:
                continue

            if raw_line.startswith("{"):
                # Baris JSON data mentah -- ini yang banjir terus-menerus,
                # dibungkam KALAU menu lagi kebuka, biar tidak mengganggu.
                if not menu_terbuka.is_set():
                    warna = warna_untuk(raw_line)
                    print(f"{warna}{raw_line}{Style.RESET_ALL}")
            else:
                # Baris BUKAN JSON (respons command seperti hasil 'P',
                # pesan [SYSTEM]/[CMD]/dst) -- SELALU tampilkan, walau menu
                # lagi kebuka, karena ini biasanya balasan LANGSUNG ke
                # command yang baru saja kamu kirim dan memang perlu dilihat.
                warna = warna_untuk(raw_line)
                print(f"{warna}{raw_line}{Style.RESET_ALL}")
                with open(SYSTEM_LOG_FILE, "a", encoding="utf-8") as f:
                    f.write(f"[{datetime.now().strftime('%H:%M:%S')}] {raw_line}\n")
            if not raw_line.startswith("{"):
                continue

            try:
                data = json.loads(raw_line)
            except json.JSONDecodeError:
                if not menu_terbuka.is_set():
                    print(f"{Fore.RED}[LOGGER] Baris JSON rusak, dilewati.{Style.RESET_ALL}")
                continue

            if data.get("type") == "session_summary":
                if menu_terbuka.is_set():
                    tulis_ringkasan_sesi(data, cetak=False)
                else:
                    tulis_ringkasan_sesi(data, cetak=True)
                continue

            # BARU: kalau kondisi diganti (nama file berubah), otomatis
            # tutup file lama, buka file baru -- data nggak akan pernah
            # nyasar ke file yang salah lagi
            if OUTPUT_CSV != current_csv_path:
                csv_file.close()
                csv_file, csv_writer = siapkan_file_csv(OUTPUT_CSV)
                current_csv_path = OUTPUT_CSV
                print(f"{Fore.GREEN}[LOGGER] Beralih menulis ke file: {OUTPUT_CSV}{Style.RESET_ALL}")

            waktu = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            baris = [
                waktu,
                data.get("rms_v"), data.get("rms_x"), data.get("rms_y"), data.get("rms_z"),
                data.get("rms_a"), data.get("cur"), data.get("cur_raw_adc"),
                data.get("temp"), data.get("temp_raw"), data.get("rpm"), data.get("snr"),
                data.get("d2"), data.get("status"),
                data.get("e_unbalance"), data.get("e_misalign"),
                data.get("e_bpfo"), data.get("e_bpfi"),
                data.get("diagnosis"), data.get("diag_conf"),
                data.get("e_audio_low"), data.get("e_audio_mid"), data.get("e_audio_high"),
                data.get("audio_diagnosis"), data.get("audio_diag_conf"),
                data.get("ml_label"), data.get("ml_conf"),
                data.get("roughness"), data.get("brightness"),
                data.get("ground_truth"),
            ]

            csv_writer.writerow(baris)
            csv_file.flush()

            baris_masuk += 1
            if baris_masuk % 20 == 0 and not menu_terbuka.is_set():
                print(f"{Fore.YELLOW}[LOGGER] {baris_masuk} baris tersimpan | "
                      f"status={data.get('status')} rpm={data.get('rpm')}{Style.RESET_ALL}")

    except KeyboardInterrupt:
        print("\n[LOGGER] Dihentikan oleh user. Menyimpan file terakhir...")
    except serial.SerialException as e:
        print(f"[LOGGER] ERROR koneksi serial: {e}")
    finally:
        stop_event.set()
        csv_file.close()
        ser.close()
        print(f"[LOGGER] Selesai. Total {baris_masuk} baris data tersimpan di {OUTPUT_CSV}")
        print(f"[LOGGER] Ringkasan sesi cek (jika ada) tersimpan di {SUMMARY_LOG_FILE}")


if __name__ == "__main__":
    main()