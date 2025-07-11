// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview This file holds dependencies that are deliberately excluded
 * from the main app.js module, to force them to load after other app.js
 * dependencies. This is done to improve performance of initial rendering of
 * core elements of the landing page, by delaying loading of non-core
 * elements (either not visible by default or not as performance critical).
 */

import './customize_dialog.js';
import './middle_slot_promo.js';
import './voice_search_overlay.js';
import './modules/module_descriptors.js';
import 'chrome://resources/cr_components/most_visited/most_visited.js';

export {PageImageServiceBrowserProxy} from 'chrome://resources/cr_components/page_image_service/browser_proxy.js';
export {CustomizeBackgroundsElement} from './customize_backgrounds.js';
export {CustomizeDialogElement} from './customize_dialog.js';
export {CustomizeModulesElement} from './customize_modules.js';
export {CustomizeShortcutsElement} from './customize_shortcuts.js';
export {LensErrorType, LensFormElement, LensSubmitType} from './lens_form.js';
export {LensUploadDialogAction, LensUploadDialogElement, LensUploadDialogError} from './lens_upload_dialog.js';
export {MiddleSlotPromoElement, PromoDismissAction} from './middle_slot_promo.js';
export {ChromeCartProxy} from './modules/cart/chrome_cart_proxy.js';
export {DiscountConsentCard, DiscountConsentVariation} from './modules/cart/discount_consent_card.js';
export {DiscountConsentDialog} from './modules/cart/discount_consent_dialog.js';
export {chromeCartDescriptor, ChromeCartModuleElement} from './modules/cart/module.js';
export {FileProxy} from './modules/drive/file_module_proxy.js';
export {driveDescriptor, DriveModuleElement} from './modules/drive/module.js';
export {FeedProxy} from './modules/feed/feed_module_proxy.js';
export {feedDescriptor, FeedModuleElement} from './modules/feed/module.js';
export {CartTileModuleElement} from './modules/history_clusters/cart/cart_tile.js';
export {HistoryClustersProxy, HistoryClustersProxyImpl} from './modules/history_clusters/history_clusters_proxy.js';
export {HistoryClusterElementType, HistoryClusterImageDisplayState, historyClustersDescriptor, HistoryClustersModuleElement, LAYOUT_1_MIN_IMAGE_VISITS, LAYOUT_1_MIN_VISITS, LAYOUT_2_MIN_IMAGE_VISITS, LAYOUT_2_MIN_VISITS, LAYOUT_3_MIN_IMAGE_VISITS, LAYOUT_3_MIN_VISITS} from './modules/history_clusters/module.js';
export {SuggestTileModuleElement} from './modules/history_clusters/suggest_tile.js';
export {TileModuleElement} from './modules/history_clusters/tile.js';
export {InfoDialogElement} from './modules/info_dialog.js';
export {InitializeModuleCallback, Module, ModuleDescriptor} from './modules/module_descriptor.js';
export {counterfactualLoad} from './modules/module_descriptors.js';
export {ModuleHeaderElement} from './modules/module_header.js';
export {ModuleRegistry} from './modules/module_registry.js';
export {ModuleWrapperElement} from './modules/module_wrapper.js';
export {DisableModuleEvent, DismissModuleEvent, ModulesElement} from './modules/modules.js';
export {photosDescriptor, PhotosModuleElement} from './modules/photos/module.js';
export {PhotosProxy} from './modules/photos/photos_module_proxy.js';
export {CalendarElement} from './modules/v2/calendar/calendar.js';
export {CalendarEventElement} from './modules/v2/calendar/calendar_event.js';
export {googleCalendarDescriptor, GoogleCalendarModuleElement} from './modules/v2/calendar/google_calendar_module.js';
export {GoogleCalendarProxyImpl} from './modules/v2/calendar/google_calendar_proxy.js';
export {outlookCalendarDescriptor, OutlookCalendarModuleElement} from './modules/v2/calendar/outlook_calendar_module.js';
// <if expr="not is_official_build">
export {FooProxy} from './modules/v2/dummy/foo_proxy.js';
export {DummyModuleElement, dummyV2Descriptor} from './modules/v2/dummy/module.js';
// </if>
export {fileSuggestionDescriptor, FileSuggestionModuleElement} from './modules/v2/file_suggestion/module.js';
export {CartTileModuleElementV2} from './modules/v2/history_clusters/cart/cart_tile.js';
export {HistoryClustersProxyImpl as HistoryClustersProxyImplV2} from './modules/v2/history_clusters/history_clusters_proxy.js';
export {HistoryClusterImageDisplayState as HistoryClusterV2ImageDisplayState, historyClustersDescriptor as historyClustersV2Descriptor, HistoryClustersModuleElement as HistoryClustersV2ModuleElement} from './modules/v2/history_clusters/module.js';
export {VisitTileModuleElement} from './modules/v2/history_clusters/visit_tile.js';
export {ModuleHeaderElementV2} from './modules/v2/module_header.js';
export {DismissModuleElementEvent, DismissModuleInstanceEvent, MODULE_CUSTOMIZE_ELEMENT_ID, ModulesV2Element, NamedWidth, SUPPORTED_MODULE_WIDTHS} from './modules/v2/modules.js';
export {mostRelevantTabResumptionDescriptor, MostRelevantTabResumptionModuleElement} from './modules/v2/most_relevant_tab_resumption/module.js';
export {MostRelevantTabResumptionProxyImpl} from './modules/v2/most_relevant_tab_resumption/most_relevant_tab_resumption_proxy.js';
export {tabResumptionDescriptor, TabResumptionModuleElement} from './modules/v2/tab_resumption/module.js';
export {TabResumptionProxyImpl} from './modules/v2/tab_resumption/tab_resumption_proxy.js';
export {VoiceSearchOverlayElement} from './voice_search_overlay.js';
