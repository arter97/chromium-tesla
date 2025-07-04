// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MANTA_MANTA_SERVICE_H_
#define COMPONENTS_MANTA_MANTA_SERVICE_H_

#include <memory>

#include "base/component_export.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "build/chromeos_buildflags.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/manta/sparky/sparky_delegate.h"
#include "components/manta/sparky/system_info_delegate.h"

namespace signin {
class IdentityManager;
}  // namespace signin

namespace network {
class SharedURLLoaderFactory;
}  // namespace network

namespace manta {
enum class FeatureSupportStatus {
  kUnknown = -1,
  kUnsupported = 0,
  kSupported = 1
};

class AnchovyProvider;
class MahiProvider;
class OrcaProvider;
class SnapperProvider;
class SparkyProvider;

// The MantaService class is a KeyedService for the Chrome/ChromeOS Manta
// project. It serves two main functions:
// 1. It hands clients instances to specific providers for calling and
// interacting with google services relevant to the Manta project.
// 2. It provides utility methods for clients to query specific information
// relevant to the Manta project.
class COMPONENT_EXPORT(MANTA) MantaService : public KeyedService {
 public:
  MantaService(
      scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory,
      signin::IdentityManager* identity_manager,
      bool is_demo_mode,
      bool is_otr_profile,
      const std::string& chrome_version,
      const std::string& locale);

  MantaService(const MantaService&) = delete;
  MantaService& operator=(const MantaService&) = delete;

  ~MantaService() override;

  // Returns a unique pointer to an instance of the Providers for the
  // profile associated with the MantaService instance from which this method
  // is called.
  std::unique_ptr<AnchovyProvider> CreateAnchovyProvider();

#if BUILDFLAG(IS_CHROMEOS_ASH)
  std::unique_ptr<MahiProvider> CreateMahiProvider();
  std::unique_ptr<OrcaProvider> CreateOrcaProvider();
  virtual std::unique_ptr<SnapperProvider> CreateSnapperProvider();
  std::unique_ptr<SparkyProvider> CreateSparkyProvider(
      std::unique_ptr<SparkyDelegate> sparky_delegate,
      std::unique_ptr<SystemInfoDelegate> system_info_delegate);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

  // Determines whether the profile for this KeyedService support Orca feature.
  FeatureSupportStatus SupportsOrca();

  // Determines whether the profile for this KeyedService can access Manta
  // features without minor restrictions.
  // If the requirements for which users can access Manta features changes, then
  // per-feature capabilities will need to be added, which will take a few weeks
  // to migrate.
  FeatureSupportStatus CanAccessMantaFeaturesWithoutMinorRestrictions();

  // KeyedService:
  void Shutdown() override;

 private:
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory_;
  raw_ptr<signin::IdentityManager> identity_manager_;
  const bool is_demo_mode_;
  const bool is_otr_profile_;
  const std::string chrome_version_;
  const std::string locale_;
};

}  // namespace manta

#endif  // COMPONENTS_MANTA_MANTA_SERVICE_H_
