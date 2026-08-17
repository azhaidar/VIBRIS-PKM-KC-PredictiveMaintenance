// DriverSuhu.cpp
#include "DriverSuhu.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include "MultiSensorFeatureMerger.h"

// Bus I2C kedua, terpisah dari LIS3DH (yang pakai TwoWire(0) di pin 6/7)
static TwoWire I2CMlx = TwoWire(1);
static Adafruit_MLX90614 mlxInstance = Adafruit_MLX90614();static volatile float g_lastRawTemp = SUHU_DEFAULT_VALID;
float DriverSuhu_GetLastRawTemp() { return g_lastRawTemp; }


void TaskDriverSuhu(void *pvParameters) {
    (void)pvParameters;

    I2CMlx.begin(PIN_MLX_SDA, PIN_MLX_SCL, 100000);

    if (!mlxInstance.begin(MLX90614_I2CADDR, &I2CMlx)) {
        for (;;) {
            Serial.println(F("[ERROR] Sensor MLX90614 (GY-906) Tidak Terdeteksi!"));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    float lastValidTemperature = SUHU_DEFAULT_VALID;

    for (;;) {
        // MLX90614 non-kontak: baca suhu OBJEK (permukaan motor),
        // bukan suhu ambient board itu sendiri
        float rawTemperature = mlxInstance.readObjectTempC();

        // FILTER 1: NaN = read I2C gagal / sensor lepas/putus
        if (!isnan(rawTemperature)) {
            g_lastRawTemp = rawTemperature;
            // FILTER 2: Slew Rate Limiter tetap dipakai, parameter sama persis
            if (abs(rawTemperature - lastValidTemperature) <= SUHU_MAX_DELTA) {
                lastValidTemperature = rawTemperature;
            } else {
                lastValidTemperature += (rawTemperature > lastValidTemperature) ? SUHU_MAX_DELTA : -SUHU_MAX_DELTA;
            }
            updateTemperatureFeature(lastValidTemperature);
        } else {
            Serial.println(F("[ERROR] Sensor MLX90614 Terputus / Malfungsi!"));
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_DELAY_SUHU));
    }
}
