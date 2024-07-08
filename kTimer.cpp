#include "kTimer.h"

kTimer::kTimer(unsigned long interval) {
    this->interval = interval;
    reset();
}

void kTimer::reset() {
    start = millis();
}

bool kTimer::expired() {
    return millis() - start >= interval;
}

unsigned long kTimer::elapsed() {
    return millis() - start;
}

unsigned long kTimer::remaining() {
    return interval - (millis() - start);
}
