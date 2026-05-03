#include <NewPing.h>

#define TRIGGER_PIN   5
#define ECHO_PIN      6
#define MAX_DISTANCE  400    // cm

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);

// ======== Cấu hình ========
#define SAMPLES        11    // Số mẫu mỗi lần đo (lẻ để lấy median)
#define PING_INTERVAL  30    // ms giữa mỗi ping
#define POLL_MS        1000  // Poll mỗi 1 giây
#define ALPHA          0.15  // Low-pass: nhỏ = mượt hơn, lớn = nhanh hơn
#define JUMP_LIMIT     100   // mm - nếu nhảy quá ngưỡng này → dùng alpha nhỏ

// ======== Lấy median từ mảng ========
int compare(const void* a, const void* b) {
  return (*(int*)a - *(int*)b);
}

int medianSamples() {
  int vals[SAMPLES];
  int valid = 0;

  for (int i = 0; i < SAMPLES; i++) {
    delay(PING_INTERVAL);
    unsigned int us = sonar.ping();
    if (us != 0) {
      vals[valid++] = us / 2 * 0.0343;  // cm
    }
  }

  if (valid == 0) return -1;   // Không có echo

  qsort(vals, valid, sizeof(int), compare);
  return vals[valid / 2];      // Median
}

// ======== Adaptive low-pass filter ========
float filtered = -1;

float applyFilter(int newVal) {
  if (filtered < 0) {
    filtered = newVal;   // Khởi tạo lần đầu
    return filtered;
  }

  float diff = abs(newVal - filtered);
  float alpha = (diff > JUMP_LIMIT) ? 0.05 : ALPHA;  // Nhảy lớn → trust ít hơn

  filtered = alpha * newVal + (1.0 - alpha) * filtered;
  return filtered;
}

void setup() {
  Serial.begin(115200);
  Serial.println("==============================");
  Serial.println("  SRO4M-2 - Stable Distance");
  Serial.println("  Samples: 11 | Median + LPF");
  Serial.println("==============================");
}

void loop() {
  static unsigned long lastPoll = 0;
  static int n = 0;

  if (millis() - lastPoll >= POLL_MS) {
    lastPoll = millis();
    n++;

    int median = medianSamples();

    Serial.print("[#"); Serial.print(n); Serial.print("] ");

    if (median < 0) {
      Serial.println("No echo - out of range!");
      return;
    }

    float smooth = applyFilter(median);

    Serial.print("Raw: ");
    Serial.print(median);
    Serial.print(" cm | Smooth: ");
    Serial.print(smooth, 1);
    Serial.print(" cm | ");
    Serial.print(smooth * 10, 0);
    Serial.println(" mm");
  }
}
