// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/webid/test/webid_test_content_browser_client.h"

#include "content/browser/webid/test/mock_modal_dialog_view_delegate.h"
#include "content/public/browser/digital_identity_provider.h"

namespace content {

WebIdTestContentBrowserClient::WebIdTestContentBrowserClient() = default;
WebIdTestContentBrowserClient::~WebIdTestContentBrowserClient() = default;

std::unique_ptr<IdentityRequestDialogController>
WebIdTestContentBrowserClient::CreateIdentityRequestDialogController(
    WebContents* rp_web_contents) {
  DCHECK(test_dialog_controller_);
  return std::move(test_dialog_controller_);
}

void WebIdTestContentBrowserClient::SetIdentityRequestDialogController(
    std::unique_ptr<IdentityRequestDialogController> controller) {
  test_dialog_controller_ = std::move(controller);
}

ContentBrowserClient::DigitalIdentityInterstitialAbortCallback
WebIdTestContentBrowserClient::ShowDigitalIdentityInterstitialIfNeeded(
    content::WebContents& web_contents,
    const url::Origin& origin,
    bool is_only_requesting_age,
    DigitalIdentityInterstitialCallback callback) {
  std::move(callback).Run(
      DigitalIdentityProvider::RequestStatusForMetrics::kSuccess);
  return base::OnceClosure();
}

std::unique_ptr<DigitalIdentityProvider>
WebIdTestContentBrowserClient::CreateDigitalIdentityProvider() {
  DCHECK(test_digital_identity_provider_);
  return std::move(test_digital_identity_provider_);
}

void WebIdTestContentBrowserClient::SetDigitalIdentityProvider(
    std::unique_ptr<DigitalIdentityProvider> provider) {
  test_digital_identity_provider_ = std::move(provider);
}

void WebIdTestContentBrowserClient::SetIdentityRegistry(
    WebContents* web_contents,
    base::WeakPtr<FederatedIdentityModalDialogViewDelegate> delegate,
    const GURL& config_url) {
  IdentityRegistry::CreateForWebContents(web_contents, delegate, config_url);
}

}  // namespace content
