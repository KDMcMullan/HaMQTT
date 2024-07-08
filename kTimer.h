#ifndef KTIMER_H
#define KTIMER_H

#include <Arduino.h>

class kTimer {
public:
  kTimer(unsigned long interval);
  void reset();
  bool expired();
  unsigned long elapsed();
  unsigned long remaining();

private:
  unsigned long interval;
  unsigned long start;
};

#endif
