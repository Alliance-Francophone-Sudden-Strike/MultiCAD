#pragma once

#include <functional>

constexpr int kMaxRenderThreads = 8;

void SetRenderThreads(int threads);
void StopRenderThreads();
void PinNewThread();
void ParallelRows(int begin, int end, int grain, const std::function<void(int first, int last)>& fn);
