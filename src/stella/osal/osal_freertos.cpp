// FreeRTOS implementation of the OS abstraction layer (ESP-IDF / Arduino-ESP32).
#if defined(ESP_PLATFORM)

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "osal.h"

namespace stella {
namespace osal {
namespace {

struct MutexState {
  StaticSemaphore_t buffer;
  SemaphoreHandle_t handle;
};

MutexState &state(unsigned char *storage) { return *reinterpret_cast<MutexState *>(storage); }

TickType_t ticks(uint32_t ms) { return ms == kForever ? portMAX_DELAY : pdMS_TO_TICKS(ms); }

}  // namespace

static_assert(sizeof(MutexState) <= 96, "Mutex storage too small");
static_assert(sizeof(MutexState) <= 160, "Signal storage too small");

Mutex::Mutex() { state(storage_).handle = xSemaphoreCreateMutexStatic(&state(storage_).buffer); }
Mutex::~Mutex() { vSemaphoreDelete(state(storage_).handle); }
bool Mutex::lock(uint32_t timeoutMs) { return xSemaphoreTake(state(storage_).handle, ticks(timeoutMs)) == pdTRUE; }
void Mutex::unlock() { xSemaphoreGive(state(storage_).handle); }

RecursiveMutex::RecursiveMutex() {
  state(storage_).handle = xSemaphoreCreateRecursiveMutexStatic(&state(storage_).buffer);
}
RecursiveMutex::~RecursiveMutex() { vSemaphoreDelete(state(storage_).handle); }
bool RecursiveMutex::lock(uint32_t timeoutMs) {
  return xSemaphoreTakeRecursive(state(storage_).handle, ticks(timeoutMs)) == pdTRUE;
}
void RecursiveMutex::unlock() { xSemaphoreGiveRecursive(state(storage_).handle); }

Signal::Signal() { state(storage_).handle = xSemaphoreCreateBinaryStatic(&state(storage_).buffer); }
Signal::~Signal() { vSemaphoreDelete(state(storage_).handle); }
void Signal::notify() { xSemaphoreGive(state(storage_).handle); }
bool Signal::wait(uint32_t timeoutMs) { return xSemaphoreTake(state(storage_).handle, ticks(timeoutMs)) == pdTRUE; }

bool startThread(const ThreadSpec &spec, ThreadEntry entry, void *arg) {
  const BaseType_t core = spec.core < 0 ? tskNO_AFFINITY : (BaseType_t)spec.core;
  return xTaskCreatePinnedToCore(entry, spec.name, spec.stackBytes, arg, spec.priority, nullptr, core) == pdPASS;
}

void sleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }

}  // namespace osal
}  // namespace stella

#endif
