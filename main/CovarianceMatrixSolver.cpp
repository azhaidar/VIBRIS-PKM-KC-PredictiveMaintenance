#include "CovarianceMatrixSolver.h"
#include <math.h>
#include <Arduino.h> // Serial, untuk warning log

// Dipakai HANYA kalau pivot mendekati nol saat proses berjalan — bukan
// dicampur ke input di awal, supaya matriks non-singular tetap dapat
// hasil presisi penuh tanpa distorsi regularisasi yang tidak perlu.
#define PIVOT_MIN_THRESHOLD   1e-6f
#define PIVOT_REGULARIZATION  1e-4f
#define SHRINKAGE_ALPHA 0.15f

void solveMatrixInverse4x4(float inputMatrix[3][3], float outputInverse[3][3]) {
    float augmented[3][6];

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            augmented[i][j] = inputMatrix[i][j];
            augmented[i][j + 3] = (i == j) ? 1.0f : 0.0f;
        }
    }

    for (int col = 0; col < 3; col++) {
        // Partial pivoting: cari baris dengan nilai absolut terbesar di kolom ini
        int pivotRow = col;
        float maxVal = fabsf(augmented[col][col]);
        for (int row = col + 1; row < 3; row++) {
            if (fabsf(augmented[row][col]) > maxVal) {
                maxVal = fabsf(augmented[row][col]);
                pivotRow = row;
            }
        }

        if (pivotRow != col) {
            for (int k = 0; k < 6; k++) {
                float tmp = augmented[col][k];
                augmented[col][k] = augmented[pivotRow][k];
                augmented[pivotRow][k] = tmp;
            }
        }

        // GUARD SINGULAR MATRIX: kalau pivot terbesar yang bisa ditemukan
        // masih mendekati nol (misal suhu ruangan nyaris tidak pernah
        // berubah selama kalibrasi), regularisasi di tempat — tambah nilai
        // kecil ke pivot itu sendiri sebelum dipakai membagi baris.
        // Ini mencegah pembagian oleh angka nyaris nol yang bikin invers
        // meledak jadi NaN/Inf, sesuai peringatan di komentar header.
        if (fabsf(augmented[col][col]) < PIVOT_MIN_THRESHOLD) {
            Serial.printf("[CovarianceSolver] WARNING: near-singular pivot di kolom %d, "
                          "regularisasi otomatis diterapkan\n", col);
            augmented[col][col] += (augmented[col][col] >= 0 ? PIVOT_REGULARIZATION : -PIVOT_REGULARIZATION);
        }

        float pivotValue = augmented[col][col];
        for (int k = 0; k < 6; k++) {
            augmented[col][k] /= pivotValue;
        }

        for (int row = 0; row < 3; row++) {
            if (row == col) continue;
            float factor = augmented[row][col];
            for (int k = 0; k < 6; k++) {
                augmented[row][k] -= factor * augmented[col][k];
            }
        }
    }

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            outputInverse[i][j] = augmented[i][j + 3];
        }
    }
}

float computeMahalanobisQuadraticForm(float currentFeatures[3], float baselineMean[3], float sigmaInverse[3][3]) {
    float diff[3];
    for (int i = 0; i < 3; i++) {
        diff[i] = currentFeatures[i] - baselineMean[i];
    }

    // temp = Sigma^-1 * diff  (matrix-vector multiply)
    float temp[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            temp[i] += sigmaInverse[i][j] * diff[j];
        }
    }

    // D^2 = diff^T * temp  (dot product, hasil skalar)
    float d2 = 0.0f;
    for (int i = 0; i < 3; i++) {
        d2 += diff[i] * temp[i];
    }

    return d2;
}

void applyShrinkageRegularization(float covariance[3][3]) {
    float traceAvg = (covariance[0][0] + covariance[1][1] + covariance[2][2]) / 3.0f;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float target = (i == j) ? traceAvg : 0.0f;
            covariance[i][j] = (1.0f - SHRINKAGE_ALPHA) * covariance[i][j] + SHRINKAGE_ALPHA * target;
        }
    }
}

bool checkMatrixWellConditioned(float covariance[3][3]) {
    for (int i = 0; i < 3; i++) {
        if (covariance[i][i] < 0.5f || covariance[i][i] > 2.0f) return false;
    }
    for (int i = 0; i < 3; i++)
        for (int j = i + 1; j < 3; j++)
            if (fabsf(covariance[i][j]) > 0.98f) return false;
    return true;
}