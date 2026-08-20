import os

# ===================== KONFIGURASI OPERASIONAL =====================
SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 115200
LOG_DIR = "logs"
if not os.path.exists(LOG_DIR):
    os.makedirs(LOG_DIR)

ESP32_USB_HINTS = ["CH340", "CH343", "CP210", "USB-SERIAL", "USB SERIAL", "FTDI", "SILICON LABS"]

# Catatan LOKAL (di dashboard, bukan di ESP32) soal slot mana yang udah
# pernah dikalibrasi -- dipakai buat nunjukin badge "Sudah Terkalibrasi" /
# "Kosong" di kartu pilih slot SEBELUM user pencet. Ini bukan pengganti
# data asli di ESP32 (baseline sungguhan tetap cuma ada di flash ESP32) --
# ini cuma catatan bantu tampilan, diisi tiap kali dashboard SENDIRI yang
# berhasil menyelesaikan kalibrasi atau menghapus baseline. Kalau file ini
# hilang (pertama kali pakai / pindah komputer), semua slot dianggap kosong
# dulu -- aman, paling jelek user cuma disuruh nunggu konfirmasi ulang,
# gak bikin data salah.
SLOT_STATE_FILE = os.path.join(LOG_DIR, "slot_calibration_state.json")

# Catatan LOKAL hasil Check TERAKHIR per slot (headline, penyebab, estimasi
# servis, jam). Ini BUKAN riwayat sensor mentah (itu ada di file CSV
# recording terpisah, lihat _init_recording di page_recording.py) -- ini
# cuma ringkasan 1 baris per slot, buat halaman Beranda/Rekomendasi (mode
# Awam) tetap bisa nunjukin "hasil Check terakhir kamu apa" walau
# aplikasi dashboard ini ditutup terus dibuka lagi. Sebelum file ini ada,
# hasil Check cuma disimpan di memori (RAM) -- begitu aplikasi Python-nya
# ditutup, "hasil terakhir" ke-reset jadi "belum ada hasil" walau
# sebenarnya kamu udah pernah Check. Itu BUKAN data ESP32 yang hilang
# (baseline & ringkasan cek ESP32 sendiri sudah tersimpan permanen di
# flash-nya, lihat CheckSession.cpp/InitialBaselineCalibrator.cpp di
# firmware) -- itu murni dashboard PC/laptop-nya yang lupa.
LAST_CHECK_FILE = os.path.join(LOG_DIR, "last_check_results.json")

# ===================== PALET WARNA INDUSTRI VIBRIS =====================
COL_BG_MAIN = "#181b20"
COL_PANEL_DARK = "#101216"
COL_ACCENT = "#38bdf8"
COL_ACCENT_DIM = "#1e2733"
COL_TEXT_LIGHT = "#e8eaed"
COL_TEXT_DIM = "#7c8592"
COL_OK = "#34d399"
COL_WARN = "#fbbf24"
COL_BAD = "#f87171"
COL_IDLE = "#94a3b8"
COL_HEADER_BG = "#1c2027"

D2_THRESHOLD_WASPADA = 9.49
D2_THRESHOLD_BAHAYA = 13.28

STATUS_SEVERITY = {"diam": -1, "normal": 0, "waspada": 1, "bahaya": 2}

# Terjemahan istilah diagnosa firmware (diagnosis_label) ke bahasa awam.
# Key di sini SUDAH dicocokkan ke DetectionResult.diagnosis_label persis
# yang ada di SharedTypes.h firmware ESP32 (UNBALANCE, MISALIGNMENT,
# BEARING_BPFO, BEARING_BPFI, NORMAL, N/A) -- dicek langsung ke repo GitHub,
# bukan tebakan. Kalau firmware nanti nambah label baru, tambahin di sini;
# label yang gak ketemu di kamus tetap ditampilkan apa adanya (gak hilang).
DIAG_LABEL_ID_MAP = {
    "unbalance": "ketidakseimbangan pada bagian yang berputar (unbalance)",
    "misalignment": "poros/kopling tidak sejajar (misalignment)",
    "bearing_bpfo": "indikasi kerusakan pada cincin luar bearing (outer race)",
    "bearing_bpfi": "indikasi kerusakan pada cincin dalam bearing (inner race)",
    "normal": "tidak ada indikasi kerusakan spesifik",
    "n/a": "belum ada diagnosa spesifik",
}

# ===================== SLOT MESIN (tetap, sesuai hardware nyata) =====================
# Firmware (main.ino) cuma dukung 2 klaster bearing sekarang:
#   command 'V' = Klaster A (~1400 RPM), command 'W' = Klaster B (~2800 RPM).
# command '0'/'1' pilih SLOT penyimpanan baseline di flash ESP32 (sebenernya
# ada slot 0-9, tapi kita cuma pakai 2 dulu sesuai motor yang beneran ada --
# gampang nambah baris baru di sini kalau nanti nambah motor).
# command 'B' = jenis bearing ROLLING (bearing bola, contoh: 6203-2RZ --
# semua motor yang ada sekarang pakai ini). Belum ada UI buat pilih BUSHING
# ('N') karena belum ada motor yang butuh -- lihat diskusi soal ini kalau
# mau nambah nanti.
SLOT_DEFS = [
    {
        "id": 0,
        "label": "Mesin 1",
        "cluster_label": "Klaster A (~1400 RPM)",
        "slot_cmd": "0",
        "cluster_cmd": "V",
        "bearing_cmd": "B",
    },
    {
        "id": 1,
        "label": "Mesin 2",
        "cluster_label": "Klaster B (~2800 RPM)",
        "slot_cmd": "1",
        "cluster_cmd": "W",
        "bearing_cmd": "B",
    },
]
# CATATAN soal nama "Mesin 1"/"Mesin 2": sebelumnya dipakai "Slot A"/"Slot B"
# -- itu istilah PENYIMPANAN di firmware ("slot" flash tempat baseline
# disimpan), bukan nama yang wajar buat operator UMKM. Diganti ke "Mesin 1"/
# "Mesin 2" karena itu yang paling gampang dimengerti tanpa perlu tau apa-apa
# soal cara kerja alat. Kalau di lapangan ada nama yang lebih pas (mis. nama
# mesin sungguhan di pabrik/kapal), TINGGAL EDIT teks "label" di atas -- gak
# ada bagian lain di kode yang perlu diubah, karena semua halaman baca nama
# ini dari sini doang (lewat _slot_display_name di dashboard_core.py).

# ===================== DURASI PROSES =====================
# HARUS SAMA dengan firmware kalau firmware-nya diubah -- ini cuma nentuin
# angka countdown yang tampil di layar. Keputusan "beneran selesai apa
# belum" tetap dari status asli yang dikirim ESP32 (lihat page_flow.py),
# bukan dari angka ini doang, biar gak meleset kalau ESP-nya sedikit
# lebih lambat/cepat dari yang diharapkan.
CALIBRATION_DURATION_S = 180   # main.ino: gerbang waktu kalibrasi via millis()
CHECK_DURATION_S = 60          # CheckSession.h: CHECK_SESSION_DURATION_MS
SLOT_SELECT_SETTLE_S = 5       # delay setelah pilih slot, sebelum baca status

STATUS_CALIBRATING = "calibrating"       # persis dari main.ino: "Calibrating"
STATUS_NOTCALIBRATED = "notcalibrated"   # slot belum pernah dikalibrasi

