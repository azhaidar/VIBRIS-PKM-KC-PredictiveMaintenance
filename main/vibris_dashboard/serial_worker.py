import os
import csv
import json
import time
from datetime import datetime

try:
    import serial
    import serial.tools.list_ports
    import threading
except ImportError:
    serial = None

from config import SERIAL_PORT, BAUD_RATE, LOG_DIR, ESP32_USB_HINTS, SLOT_DEFS

class SerialWorkerMixin:
    """Semua urusan komunikasi serial ke ESP32: cari port, baca JSON,
    kirim command, dan auto-snapshot pas status berubah jadi waspada/bahaya.
    Method di sini murni I/O -- gak nyentuh widget sama sekali, cuma nulis
    ke self.current_v/self.current_a/dst yang dibaca ulang oleh _update_gui
    di dashboard_core.py."""

    def _init_serial_connection(self):
        if serial is not None:
            t = threading.Thread(target=self._read_serial_worker, daemon=True)
            t.start()


    def _resolve_serial_port(self):
        try:
            ports = list(serial.tools.list_ports.comports())
        except Exception:
            ports = []

        if not ports:
            return None

        available = [p.device for p in ports]
        if SERIAL_PORT in available:
            return SERIAL_PORT

        for p in ports:
            desc = f"{p.description} {p.manufacturer or ''}".upper()
            if any(hint in desc for hint in ESP32_USB_HINTS):
                return p.device

        if len(ports) == 1:
            return ports[0].device
        return None


    def _read_serial_worker(self):
        while True:
            try:
                if self.ser is None or not self.ser.is_open:
                    port_to_use = self._resolve_serial_port()
                    if not port_to_use:
                        self.serial_connected = False
                        time.sleep(2)
                        continue

                    self.ser = serial.Serial(port_to_use, BAUD_RATE, timeout=1)
                    self.serial_connected = True

                raw = self.ser.readline()
                if not raw:
                    if time.time() - self.last_packet_time > 2.5:
                        self.packet_loss_flag = True
                    continue

                self.last_packet_time = time.time()
                self.packet_loss_flag = False

                line = raw.decode('utf-8', errors='ignore').strip()
                if not line.startswith("{"):
                    continue

                try:
                    data = json.loads(line)
                except (json.JSONDecodeError, ValueError):
                    continue

                self.last_raw_line = line

                self.current_v = float(data.get("rms_v", 0.0))
                self.current_a = float(data.get("rms_a", 0.0))
                self.current_temp = float(data.get("temp", 0.0))
                # PENTING: key JSON asli dari ESP32 itu "rms_x"/"rms_y"/"rms_z"
                # (cek RaspberryPiDataTransmitter.cpp baris 33-40 di firmware),
                # BUKAN "vib_x"/"vib_y"/"vib_z". Sebelum ini dibetulkan, .get()
                # gak pernah nemu key-nya, jadi diam-diam selalu balik ke
                # default 0.0 -- getaran per-sumbu di Summary keliatan "jalan"
                # padahal angkanya palsu semua.
                self.current_vx = float(data.get("rms_x", 0.0))
                self.current_vy = float(data.get("rms_y", 0.0))
                self.current_vz = float(data.get("rms_z", 0.0))
                self.current_rpm = float(data.get("rpm", 0.0))
                self.current_d2 = float(data.get("d2", 0.0))
                self.current_status_device = data.get("status", "")

                self.current_health_score = float(data.get("health_score", 100.0))
                self.current_trend = data.get("trend", "Mengumpulkan")
                self.current_servis = data.get("servis_estimasi", "30+ hari")
                self.current_ml_label = data.get("ml_label", "N/A")
                # Sama kasusnya: firmware kirim "ml_conf" (bukan "ml_confidence"),
                # "diagnosis" (bukan "diagnosis_label"), dan "diag_conf" (bukan
                # "diagnosis_confidence") -- lihat printf di
                # RaspberryPiDataTransmitter.cpp baris 38 & 46.
                self.current_ml_conf = float(data.get("ml_conf", 0.0))
                self.current_diag_label = data.get("diagnosis", "N/A")
                self.current_diag_conf = float(data.get("diag_conf", 0.0))

                # "kurtosis" MASIH belum dikirim firmware sama sekali (lihat
                # catatan perbaikan ESP32 yang saya kasih terpisah) -- jadi ini
                # tetap 0.0 sampai firmware-nya dibetulkan juga. Sengaja saya
                # ganti default dari 3.0 ke 0.0: 3.0 itu nilai kurtosis
                # distribusi Normal sempurna, jadi kalau dibiarkan 3.0 dia
                # KELIHATAN kayak data asli yang bilang "getarannya normal",
                # padahal itu cuma angka kosong. Lebih jujur ditampilkan 0.0.
                self.current_kurtosis = float(data.get("kurtosis", 0.0))
                # firmware kirim "diag_flags" berupa ANGKA bitmask (bit0=unbalance,
                # bit1=misalignment, bit2=BPFO, bit3=BPFI -- lihat SharedTypes.h
                # baris 56), bukan teks "diagnosis_flags". _decode_diag_flags()
                # di bawah nerjemahin angka itu jadi teks yang bisa dibaca orang.
                self.current_diagnosis_flags = self._decode_diag_flags(data.get("diag_flags", 0))

                self.fft_hz_buffer = data.get("fft_hz", [])
                self.fft_mag_buffer = data.get("fft_mag", [])

                self.tick += 1
                self.time_buffer.append(self.tick)
                self.v_buffer.append(self.current_v)
                self.vx_buffer.append(self.current_vx)
                self.vy_buffer.append(self.current_vy)
                self.vz_buffer.append(self.current_vz)
                self.a_buffer.append(self.current_a)
                self.temp_buffer.append(self.current_temp)
                self.rpm_buffer.append(self.current_rpm)
                self.d2_buffer.append(self.current_d2)

                self.hist_v_buf.append(self.current_v)
                self.hist_a_buf.append(self.current_a)
                self.hist_temp_buf.append(self.current_temp)
                self.hist_vx_buf.append(self.current_vx)
                self.hist_vy_buf.append(self.current_vy)
                self.hist_vz_buf.append(self.current_vz)
                self.hist_rpm_buf.append(self.current_rpm)
                self.hist_d2_buf.append(self.current_d2)

                current_st_lower = (self.current_status_device or "").strip().lower()
                if current_st_lower in ("waspada", "bahaya") and self.last_auto_snapshot_status == "normal":
                    self._trigger_auto_event_snapshot(current_st_lower)
                self.last_auto_snapshot_status = current_st_lower

                if self.recording and self.csv_writer:
                    elapsed = time.perf_counter() - self.record_start_time
                    machine_name = (SLOT_DEFS[self.selected_slot_idx]["label"]
                                     if self.selected_slot_idx >= 0 else "Belum Dipilih")
                    self.csv_writer.writerow([
                        round(elapsed, 3), machine_name,
                        self.current_v, self.current_a, self.current_temp,
                        self.current_vx, self.current_vy, self.current_vz,
                        self.current_rpm, self.current_d2, self.current_status_device,
                        self.current_health_score, self.current_trend, self.current_servis,
                        self.current_ml_label, self.current_diag_label, self.current_kurtosis
                    ])
                    self.csv_file.flush()
            except Exception as e:
                print(f"[SERIAL] status: {e}")
                self.serial_connected = False
                self.ser = None
                self.current_v = self.current_a = self.current_temp = None
                time.sleep(2)


    @staticmethod
    def _decode_diag_flags(flags_value):
        """Uraikan bitmask 'diag_flags' dari ESP32 jadi teks yang gampang
        dibaca. Urutan bit HARUS sama persis dengan SharedTypes.h baris 56
        di firmware (bit0=unbalance, bit1=misalignment, bit2=BPFO,
        bit3=BPFI) -- kalau urutan itu diubah di firmware, urutan di sini
        juga wajib ikut diubah, atau labelnya bakal ketuker."""
        try:
            flags_int = int(flags_value)
        except (TypeError, ValueError):
            return "Aman"
        labels = []
        if flags_int & 0b0001:
            labels.append("Unbalance")
        if flags_int & 0b0010:
            labels.append("Misalignment")
        if flags_int & 0b0100:
            labels.append("BPFO (outer race)")
        if flags_int & 0b1000:
            labels.append("BPFI (inner race)")
        return ", ".join(labels) if labels else "Aman"


    def _trigger_auto_event_snapshot(self, status_severity_str):
        try:
            snapshot_filename = os.path.join(LOG_DIR, f"snapshot_{status_severity_str.upper()}_{datetime.now().strftime('%d%m%Y_%H%M%S')}.csv")
            with open(snapshot_filename, 'w', newline='') as sf:
                sw = csv.writer(sf)
                sw.writerow([
                    'snapshot_index', 'rms_v', 'rms_a', 'temp', 'vib_x', 'vib_y', 'vib_z', 
                    'rpm', 'mahalanobis_d2', 'triggered_status'
                ])
                for idx_snap in range(len(self.hist_v_buf)):
                    sw.writerow([
                        idx_snap, self.hist_v_buf[idx_snap], self.hist_a_buf[idx_snap],
                        self.hist_temp_buf[idx_snap], self.hist_vx_buf[idx_snap],
                        self.hist_vy_buf[idx_snap], self.hist_vz_buf[idx_snap],
                        self.hist_rpm_buf[idx_snap], self.hist_d2_buf[idx_snap],
                        status_severity_str
                    ])
        except Exception as ex:
            print(f"[AUTO-SNAPSHOT ERROR] {ex}")


    def _send_command(self, cmd_char):
        if self.ser is not None and self.ser.is_open:
            try:
                self.ser.write(cmd_char.encode())
            except Exception as e:
                print(f"[SERIAL] Gagal kirim command '{cmd_char}': {e}")
        else:
            print(f"[SERIAL] Command '{cmd_char}' tidak terkirim -- belum tersambung.")
            
