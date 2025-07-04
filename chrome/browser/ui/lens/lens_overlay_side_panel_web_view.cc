// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/lens/lens_overlay_side_panel_web_view.h"

#include "chrome/browser/file_select_helper.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/lens/lens_overlay_controller.h"
#include "chrome/browser/ui/lens/lens_overlay_event_handler.h"
#include "chrome/browser/ui/lens/lens_overlay_side_panel_coordinator.h"
#include "chrome/browser/ui/lens/lens_untrusted_ui.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/generated_resources.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/file_select_listener.h"
#include "ui/base/metadata/metadata_impl_macros.h"

using SidePanelWebUIViewT_LensUntrustedUI =
    SidePanelWebUIViewT<lens::LensUntrustedUI>;
BEGIN_TEMPLATE_METADATA(SidePanelWebUIViewT_LensUntrustedUI,
                        SidePanelWebUIViewT)
END_METADATA

namespace {

Browser* BrowserFromWebContents(content::WebContents* web_contents) {
  BrowserWindow* window =
      BrowserWindow::FindBrowserWindowWithWebContents(web_contents);
  auto* browser_view = static_cast<BrowserView*>(window);
  if (browser_view) {
    return browser_view->browser();
  }
  return nullptr;
}

}  // namespace

LensOverlaySidePanelWebView::LensOverlaySidePanelWebView(
    content::BrowserContext* browser_context,
    lens::LensOverlaySidePanelCoordinator* coordinator)
    : SidePanelWebUIViewT(
          base::RepeatingClosure(),
          base::RepeatingClosure(),
          std::make_unique<WebUIContentsWrapperT<lens::LensUntrustedUI>>(
              GURL(chrome::kChromeUILensUntrustedSidePanelURL),
              browser_context,
              /*task_manager_string_id=*/IDS_SIDE_PANEL_COMPANION_TITLE,
              /*webui_resizes_host=*/false,
              /*esc_closes_ui=*/false)),
      coordinator_(coordinator) {}

LensOverlaySidePanelWebView::~LensOverlaySidePanelWebView() {
  if (coordinator_) {
    coordinator_->WebViewClosing();
    coordinator_ = nullptr;
  }
}

void LensOverlaySidePanelWebView::ClearCoordinator() {
  coordinator_ = nullptr;
}

bool LensOverlaySidePanelWebView::HandleContextMenu(
    content::RenderFrameHost& render_frame_host,
    const content::ContextMenuParams& params) {
  return false;
}

content::WebContents* LensOverlaySidePanelWebView::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  Browser* browser = BrowserFromWebContents(web_contents());
  if (browser) {
    browser->OpenURL(params, std::move(navigation_handle_callback));
  }
  return nullptr;
}

bool LensOverlaySidePanelWebView::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (!coordinator_) {
    return false;
  }
  return coordinator_->GetLensOverlayController()
      ->lens_overlay_event_handler()
      ->HandleKeyboardEvent(source, event, GetFocusManager());
}

void LensOverlaySidePanelWebView::RequestMediaAccessPermission(
    content::WebContents* web_contents,
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback) {
  // Note: This is needed for taking screenshots via the feedback form on the
  // side panel.
  MediaCaptureDevicesDispatcher::GetInstance()->ProcessMediaAccessRequest(
      web_contents, request, std::move(callback), /*extension=*/nullptr);
}

BEGIN_METADATA(LensOverlaySidePanelWebView)
END_METADATA
