// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MANTA_SPARKY_SYSTEM_INFO_DELEGATE_H_
#define COMPONENTS_MANTA_SPARKY_SYSTEM_INFO_DELEGATE_H_

#include <memory>

#include "base/component_export.h"
#include "base/functional/callback_forward.h"
#include "base/observer_list_types.h"

namespace manta {

enum class Diagnostics { kBattery, kCpu, kStorage, kMemory };

struct COMPONENT_EXPORT(MANTA) StorageData {
  std::string in_use_bytes;
  std::string total_bytes;

  StorageData(const std::string& in_use_bytes, const std::string& total_bytes);
  ~StorageData();

  StorageData(const StorageData&) = delete;
  StorageData& operator=(const StorageData&) = delete;
};

struct COMPONENT_EXPORT(MANTA) BatteryData {
  int cycle_count;
  int battery_wear_percentage;
  std::string power_time;
  int battery_percentage;

  BatteryData(int cycle_count,
              int battery_wear_percentage,
              const std::string& power_time,
              int battery_percentage);
  ~BatteryData();

  BatteryData(const BatteryData&) = delete;
  BatteryData& operator=(const BatteryData&) = delete;
};

struct COMPONENT_EXPORT(MANTA) CpuData {
  int cpu_usage_percentage_snapshot;
  int average_cpu_temp_celsius;
  double scaling_current_frequency_ghz;

  CpuData(int cpu_usage_percentage_snapshot,
          int average_cpu_temp_celsius,
          double scaling_current_frequency_ghz);

  ~CpuData();

  CpuData(const CpuData&) = delete;
  CpuData& operator=(const CpuData&) = delete;
};

struct COMPONENT_EXPORT(MANTA) MemoryData {
  double available_memory_gb;
  double total_memory_gb;

  MemoryData(double available_memory_gb, double total_memory_gb);

  ~MemoryData();

  MemoryData(const MemoryData&) = delete;
  MemoryData& operator=(const MemoryData&) = delete;
};

struct COMPONENT_EXPORT(MANTA) DiagnosticsData {
  std::unique_ptr<BatteryData> battery_data;
  std::unique_ptr<CpuData> cpu_data;
  std::unique_ptr<MemoryData> memory_data;

  DiagnosticsData(std::unique_ptr<BatteryData> battery_data,
                  std::unique_ptr<CpuData> cpu_data,
                  std::unique_ptr<MemoryData> memory_data);

  ~DiagnosticsData();

  DiagnosticsData(const DiagnosticsData&) = delete;
  DiagnosticsData& operator=(const DiagnosticsData&) = delete;
};

using DiagnosticsDataCallback =
    base::OnceCallback<void(std::unique_ptr<DiagnosticsData>)>;

class COMPONENT_EXPORT(MANTA) SystemInfoDelegate {
 public:
  SystemInfoDelegate();
  SystemInfoDelegate(const SystemInfoDelegate&) = delete;
  SystemInfoDelegate& operator=(const SystemInfoDelegate&) = delete;

  virtual ~SystemInfoDelegate();

  virtual void ObtainDiagnostics(
      const std::vector<Diagnostics>& diagnostics,
      DiagnosticsDataCallback diagnostics_callback) = 0;
};

}  // namespace manta

#endif  // COMPONENTS_MANTA_SPARKY_SYSTEM_INFO_DELEGATE_H_
