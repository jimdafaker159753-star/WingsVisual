#pragma once
// Финальная сборка: логирование ESP отключено.
// Файл WingsVisual_ESP.log НЕ создаётся, память не засоряется.
static inline void EspLog(const char* /*fmt*/, ...) {}
static inline void EspLogReset() {}