// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import './object_layer.js';
import './text_layer.js';
import './region_selection.js';
import './post_selection_renderer.js';
import './overlay_shimmer.js';
import './overlay_shimmer_canvas.js';
import './strings.m.js';
import '//resources/cr_elements/cr_button/cr_button.js';
import '//resources/cr_elements/cr_toast/cr_toast.js';

import {I18nMixin} from '//resources/cr_elements/i18n_mixin.js';
import {assert} from '//resources/js/assert.js';
import {EventTracker} from '//resources/js/event_tracker.js';
import {loadTimeData} from '//resources/js/load_time_data.js';
import {PolymerElement} from '//resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {BrowserProxyImpl} from './browser_proxy.js';
import type {BrowserProxy} from './browser_proxy.js';
import {getFallbackTheme} from './color_utils.js';
import {type CursorTooltipData, CursorTooltipType} from './cursor_tooltip.js';
import {UserAction} from './lens.mojom-webui.js';
import {INVOCATION_SOURCE} from './lens_overlay_app.js';
import {recordLensOverlayInteraction} from './metrics_utils.js';
import type {ObjectLayerElement} from './object_layer.js';
import type {OverlayShimmerElement} from './overlay_shimmer.js';
import type {OverlayShimmerCanvasElement} from './overlay_shimmer_canvas.js';
import type {PostSelectionRendererElement} from './post_selection_renderer.js';
import type {RegionSelectionElement} from './region_selection.js';
import {getTemplate} from './selection_overlay.html.js';
import {CursorType, DRAG_THRESHOLD, DragFeature, emptyGestureEvent, focusShimmerOnRegion, GestureState, ShimmerControlRequester, unfocusShimmer} from './selection_utils.js';
import type {GestureEvent, OverlayShimmerFocusedRegion} from './selection_utils.js';
import type {TextLayerElement} from './text_layer.js';
import {toPercent} from './values_converter.js';

const RESIZE_THRESHOLD = 8;

// The size of our custom cursor.
export const CURSOR_SIZE_PIXEL = 32;

// The cursor image url css variable name.
export const CURSOR_IMG_URL = '--cursor-img-url';

export interface CursorData {
  cursor: CursorType;
}

export interface SelectedTextContextMenuData {
  // The text selection that the context menu commands will act on.
  text: string;
  // Dominant content language of the text. Language code is CLDR/BCP-47.
  contentLanguage: string;
  // The left-most position of the selected text.
  left: number;
  // The right-most position of the selected text.
  right: number;
  // The highest position of the selected text.
  top: number;
  // The lowest position of the selected text.
  bottom: number;
  // The selection start index of the text.
  selectionStartIndex: number;
  // The end selection index of the text.
  selectionEndIndex: number;
}

export interface DetectedTextContextMenuData {
  // The left-most position of the detected text.
  left: number;
  // The right-most position of the detected text.
  right: number;
  // The highest position of the detected text.
  top: number;
  // The lowest position of the detected text.
  bottom: number;
  // The selection start index of the text.
  selectionStartIndex: number;
  // The end selection index of the text.
  selectionEndIndex: number;
}

export interface SelectionOverlayElement {
  $: {
    backgroundImage: HTMLImageElement,
    cursor: HTMLElement,
    detectedTextContextMenu: HTMLElement,
    initialFlashScrim: HTMLDivElement,
    objectSelectionLayer: ObjectLayerElement,
    overlayShimmerCanvas: OverlayShimmerCanvasElement,
    overlayShimmer: OverlayShimmerElement,
    postSelectionRenderer: PostSelectionRendererElement,
    regionSelectionLayer: RegionSelectionElement,
    selectedTextContextMenu: HTMLElement,
    selectionOverlay: HTMLElement,
    textSelectionLayer: TextLayerElement,
  };
}

const SelectionOverlayElementBase = I18nMixin(PolymerElement);

/*
 * Element responsible for coordinating selections between the various selection
 * features. This includes:
 *   - Storing state needed to coordinate selections across features
 *   - Listening to mouse/tap events and delegating them to the correct features
 *   - Coordinating animations between the different features
 */
export class SelectionOverlayElement extends SelectionOverlayElementBase {
  static get is() {
    return 'lens-selection-overlay';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      isResized: {
        type: Boolean,
        reflectToAttribute: true,
      },
      isInitialSize: {
        type: Boolean,
        reflectToAttribute: true,
      },
      showSelectedTextContextMenu: {
        type: Boolean,
        value: false,
        reflectToAttribute: true,
      },
      showDetectedTextContextMenu: {
        type: Boolean,
        value: false,
        reflectToAttribute: true,
      },
      selectedTextContextMenuX: Number,
      selectedTextContextMenuY: Number,
      detectedTextContextMenuX: Number,
      detectedTextContextMenuY: Number,
      screenshotDataUri: String,
      isPointerInside: Boolean,
      currentGesture: emptyGestureEvent(),
      disableShimmer: {
        type: Boolean,
        readOnly: true,
        value: !loadTimeData.getBoolean('enableShimmer'),
      },
      useShimmerCanvas: {
        type: Boolean,
        readOnly: true,
        value: loadTimeData.getBoolean('useShimmerCanvas'),
      },
      isClosing: {
        type: Boolean,
        reflectToAttribute: true,
      },
      shimmerOnSegmentation: {
        type: Boolean,
        reflectToAttribute: true,
      },
      shimmerFadeOutComplete: {
        type: Boolean,
        reflectToAttribute: true,
      },
      darkenExtraScrim: {
        type: Boolean,
        reflectToAttribute: true,
      },
      theme: {
        type: Object,
        value: getFallbackTheme,
      },
    };
  }

  // Whether the selection overlay is its initial size, or has changed size.
  private isResized: boolean = false;
  private isInitialSize: boolean = true;
  private showSelectedTextContextMenu: boolean;
  private showDetectedTextContextMenu: boolean;
  // Location at which to show the context menus.
  private selectedTextContextMenuX: number;
  private selectedTextContextMenuY: number;
  private detectedTextContextMenuX: number;
  private detectedTextContextMenuY: number;
  private highlightedText: string = '';
  private contentLanguage: string = '';
  private textSelectionStartIndex: number = -1;
  private textSelectionEndIndex: number = -1;
  private detectedTextStartIndex: number = -1;
  private detectedTextEndIndex: number = -1;
  // The data URI of the current overlay screenshot.
  private screenshotDataUri: string;
  private isPointerInside = false;
  private isPointerInsideContextMenu = false;
  // The current gesture event. The coordinate values are only accurate if a
  // gesture has started.
  private currentGesture: GestureEvent = emptyGestureEvent();
  private disableShimmer: boolean;
  private useShimmerCanvas: boolean;
  // Whether the overlay is being shut down.
  private isClosing: boolean = false;
  // Whether the default background scrim is currently being darkened.
  private darkenExtraScrim: boolean = false;
  // Whether the shimmer is currently focused on a segmentation mask.
  private shimmerOnSegmentation: boolean = false;
  private shimmerFadeOutComplete: boolean = true;

  private eventTracker_: EventTracker = new EventTracker();
  // Listener ids for events from the browser side.
  private listenerIds: number[];
  // The feature currently being dragged. Once a feature responds to a drag
  // event, no other feature will receive gesture events.
  private draggingRespondent = DragFeature.NONE;
  private resizeObserver: ResizeObserver = new ResizeObserver(() => {
    this.handleResize();
  });
  // We need to listen to resizes on the selectionElements separately, since
  // resizeObserver will trigger before the selectionElements have a chance to
  // resize.
  private selectionElementsResizeObserver: ResizeObserver =
      new ResizeObserver(() => {
        this.handleSelectionElementsResize();
      });
  // Used to listen for changes in the window.devicePixelRatio. Stored as a
  // variable so we can easily add and remove the listener.
  private matchMedia?: MediaQueryList;
  private initialWidth: number = 0;
  private initialHeight: number = 0;
  private cursorOffsetX: number = 3;
  private cursorOffsetY: number = 6;
  private hasInitialFlashAnimationEnded = false;
  private browserProxy: BrowserProxy = BrowserProxyImpl.getInstance();

  override connectedCallback() {
    super.connectedCallback();
    this.resizeObserver.observe(this);
    this.selectionElementsResizeObserver.observe(this.$.selectionOverlay);
    this.listenerIds = [
      this.browserProxy.callbackRouter.notifyOverlayClosing.addListener(() => {
        this.isClosing = true;
        this.removeDragListeners();
      }),
      this.browserProxy.callbackRouter.triggerCopyText.addListener(() => {
        this.handleCopy();
      }),
    ];
    this.eventTracker_.add(
        document, 'shimmer-fade-out-complete', (e: CustomEvent<boolean>) => {
          this.shimmerFadeOutComplete = e.detail;
        });
    this.eventTracker_.add(
        document, 'set-cursor', (e: CustomEvent<CursorData>) => {
          if (e.detail.cursor === CursorType.POINTER) {
            this.setCursorToPointer();
          } else if (e.detail.cursor === CursorType.CROSSHAIR) {
            this.setCursorToCrosshair();
          } else if (e.detail.cursor === CursorType.TEXT) {
            this.setCursorToText();
          } else {
            this.resetCursor();
          }
        });
    this.eventTracker_.add(
        document, 'show-selected-text-context-menu',
        (e: CustomEvent<SelectedTextContextMenuData>) => {
          this.showSelectedTextContextMenu = true;
          this.selectedTextContextMenuX = e.detail.left;
          this.selectedTextContextMenuY = e.detail.bottom;
          this.highlightedText = e.detail.text;
          this.contentLanguage = e.detail.contentLanguage;
          this.textSelectionStartIndex = e.detail.selectionStartIndex;
          this.textSelectionEndIndex = e.detail.selectionEndIndex;
        });
    this.eventTracker_.add(
        document, 'restore-selected-text-context-menu', () => {
          // show-selected-text-context-menu must be triggered first so that
          // instance variables are set.
          this.showSelectedTextContextMenu = true;
        });
    this.eventTracker_.add(
        document, 'show-detected-text-context-menu', (e: CustomEvent) => {
          this.showDetectedTextContextMenu = true;
          this.detectedTextContextMenuX = e.detail.left;
          this.detectedTextContextMenuY = e.detail.bottom;
          this.detectedTextStartIndex = e.detail.selectionStartIndex;
          this.detectedTextEndIndex = e.detail.selectionEndIndex;
        });
    this.eventTracker_.add(document, 'hide-selected-text-context-menu', () => {
      this.showSelectedTextContextMenu = false;
      this.textSelectionStartIndex = -1;
      this.textSelectionEndIndex = -1;
    });
    this.eventTracker_.add(document, 'hide-detected-text-context-menu', () => {
      this.showDetectedTextContextMenu = false;
      this.detectedTextStartIndex = -1;
      this.detectedTextEndIndex = -1;
    });
    this.eventTracker_.add(document, 'darken-extra-scrim-opacity', () => {
      this.darkenExtraScrim = true;
    });
    this.eventTracker_.add(document, 'lighten-extra-scrim-opacity', () => {
      this.darkenExtraScrim = false;
    });
    this.eventTracker_.add(
        this.$.initialFlashScrim, 'animationend', (event: AnimationEvent) => {
          // The flash animation is the longest animation.
          if (event.animationName !== 'initial-inset-animation') {
            return;
          }

          this.onInitialFlashAnimationEnd();
        });
    this.eventTracker_.add(
        document, 'focus-region',
        (e: CustomEvent<OverlayShimmerFocusedRegion>) => {
          if (e.detail.requester === ShimmerControlRequester.SEGMENTATION) {
            this.shimmerOnSegmentation = true;
          }
        });
    this.eventTracker_.add(document, 'unfocus-region', () => {
      this.shimmerOnSegmentation = false;
    });

    this.updateDevicePixelRatioListener();
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.resizeObserver.unobserve(this);
    this.selectionElementsResizeObserver.unobserve(this.$.selectionOverlay);
    this.eventTracker_.removeAll();
    this.listenerIds.forEach(
        id => assert(this.browserProxy.callbackRouter.removeListener(id)));
    this.listenerIds = [];

    assert(this.matchMedia);
    this.matchMedia.removeEventListener(
        'change', this.onDevicePixelRatioChanged.bind(this));
  }

  override ready() {
    super.ready();
    this.addEventListener('pointerdown', this.onPointerDown.bind(this));
    this.addEventListener('pointermove', this.updateCursorPosition.bind(this));
  }

  private addDragListeners() {
    this.addEventListener('pointerup', this.onPointerUp);
    this.addEventListener('pointermove', this.onPointerMove);
    this.addEventListener('pointercancel', this.onPointerCancel);
  }

  private removeDragListeners() {
    this.removeEventListener('pointerup', this.onPointerUp);
    this.removeEventListener('pointermove', this.onPointerMove);
    this.removeEventListener('pointercancel', this.onPointerCancel);
  }

  private updateDevicePixelRatioListener() {
    // Remove the previous listener since we are now listening for a different
    // pixel ratio change.
    if (this.matchMedia) {
      this.eventTracker_.remove(this.matchMedia, 'change');
    }

    // Listen to changes to the current device pixel ratio.
    const queryString = `(resolution: ${window.devicePixelRatio}dppx)`;
    this.matchMedia = matchMedia(queryString);
    this.eventTracker_.add(
        this.matchMedia, 'change', this.onDevicePixelRatioChanged.bind(this));
  }

  private onDevicePixelRatioChanged() {
    // Update the listener to the new pixel ratio.
    this.updateDevicePixelRatioListener();
    // Handle UI updates.
    this.handleSelectionElementsResize();
  }

  private updateCursorPosition(event: PointerEvent) {
    const mouseX = event.clientX;
    const mouseY = event.clientY;

    const cursorOffsetX = mouseX + this.cursorOffsetX;
    const cursorOffsetY = mouseY + this.cursorOffsetY;

    if (!this.disableShimmer &&
        (this.isPointerInside ||
         this.currentGesture.state === GestureState.DRAGGING)) {
      this.updateShimmerForCursor(cursorOffsetX, cursorOffsetY);
    }

    this.$.cursor.style.transform =
        `translate3d(${cursorOffsetX}px, ${cursorOffsetY}px, 0)`;
  }

  private updateShimmerForCursor(cursorLeft: number, cursorTop: number) {
    const boundingRect = this.$.selectionOverlay.getBoundingClientRect();

    const relativeXPercent =
        Math.max(
            0, Math.min(cursorLeft, boundingRect.right) - boundingRect.left) /
        boundingRect.width;
    const relativeYPercent =
        Math.max(
            0, Math.min(cursorTop, boundingRect.bottom) - boundingRect.top) /
        boundingRect.height;

    focusShimmerOnRegion(
        this, relativeYPercent, relativeXPercent,
        CURSOR_SIZE_PIXEL / boundingRect.width,
        CURSOR_SIZE_PIXEL / boundingRect.height,
        ShimmerControlRequester.CURSOR);
  }

  private getHiddenCursorClass(isPointerInside: boolean, state: GestureState):
      string {
    // Always show when dragging, even if outside the selection overlay.
    if (!isPointerInside && state !== GestureState.DRAGGING) {
      return 'hidden';
    } else {
      return '';
    }
  }

  // LINT.IfChange(CursorOffsetValues)
  // Called on text hover and drag.
  private setCursorToText() {
    // Set body cursor style to handle dragging.
    document.body.style.cursor = 'text';
    this.cursorOffsetX = 3;
    this.cursorOffsetY = 8;
    this.style.setProperty(CURSOR_IMG_URL, 'url("text.svg")');
  }

  // Called on region selection drag.
  private setCursorToCrosshair() {
    // Set body cursor style to handle dragging.
    document.body.style.cursor = 'crosshair';
    this.cursorOffsetX = 3;
    this.cursorOffsetY = 6;
    this.style.setProperty(CURSOR_IMG_URL, 'url("lens.svg")');
  }

  // Called on object hover.
  private setCursorToPointer() {
    // No dragging for objects, so no need to set body cursor style.
    this.cursorOffsetX = 4;
    this.cursorOffsetY = 8;
    this.style.setProperty(CURSOR_IMG_URL, 'url("lens.svg")');
  }

  private resetCursor() {
    document.body.style.cursor = 'unset';
    this.cursorOffsetX = 3;
    this.cursorOffsetY = 6;
    this.style.setProperty(CURSOR_IMG_URL, 'url("lens.svg")');
  }
  // LINT.ThenChange(//chrome/browser/resources/lens/overlay/cursor_tooltip.ts:CursorOffsetValues)

  private handlePointerEnter() {
    this.isPointerInside = true;
    if (!this.isPointerInsideContextMenu) {
      this.dispatchEvent(
          new CustomEvent<CursorTooltipData>('set-cursor-tooltip', {
            bubbles: true,
            composed: true,
            detail: {tooltipType: CursorTooltipType.REGION_SEARCH},
          }));
    }
  }

  private handlePointerLeave() {
    this.isPointerInside = false;

    // Unfocus the shimmer from the cursor. If the cursor is dragging, force
    // shimmer to follow cursor.
    if (!this.disableShimmer &&
        this.currentGesture.state !== GestureState.DRAGGING) {
      unfocusShimmer(this, ShimmerControlRequester.CURSOR);
    }
  }

  private onImageLoad() {
    // The image is loaded, but not necessarily rendered to the user. To avoid
    // adding the background scrim too early and it being noticeable to the
    // user, we wait for two animation frames before notifying that the image is
    // visible.
    requestAnimationFrame(() => {
      requestAnimationFrame(() => {
        this.onImageRendered();
      });
    });
  }

  private onImageRendered() {
    // Let the parent know it is safe to blur the background.
    this.dispatchEvent(new CustomEvent(
        'screenshot-rendered', {bubbles: true, composed: true}));

    // Tell the browser to blur the background on next animation frame.
    requestAnimationFrame(() => {
      this.browserProxy.handler.addBackgroundBlur();
    });
  }

  private onPointerDown(event: PointerEvent) {
    if (this.shouldIgnoreEvent(event)) {
      return;
    }

    if (event.button === 2 /* right button */) {
      this.$.textSelectionLayer.handleRightClick(event);
      return;
    }

    this.dispatchEvent(new CustomEvent(
        'selection-overlay-clicked', {bubbles: true, composed: true}));
    this.addDragListeners();
    this.browserProxy.handler.closeSearchBubble();
    this.browserProxy.handler.closePreselectionBubble();

    this.currentGesture = {
      state: GestureState.STARTING,
      startX: event.clientX,
      startY: event.clientY,
      clientX: event.clientX,
      clientY: event.clientY,
    };

    if (this.$.textSelectionLayer.handleDownGesture(this.currentGesture)) {
      // Text is responding to this sequence of gestures.
      this.draggingRespondent = DragFeature.TEXT;
      this.$.postSelectionRenderer.clearSelection();
    } else if (this.$.postSelectionRenderer.handleDownGesture(
                   this.currentGesture)) {
      this.draggingRespondent = DragFeature.POST_SELECTION;
    }
  }

  private onPointerUp(event: PointerEvent) {
    this.updateGestureCoordinates(event);

    // Allow proper feature to respond to the tap/drag event.
    switch (this.currentGesture.state) {
      case GestureState.DRAGGING:
        // Drag has finished. Let the features respond to the end of a drag.
        if (this.draggingRespondent === DragFeature.MANUAL_REGION) {
          this.$.regionSelectionLayer.handleUpGesture(this.currentGesture);
        } else if (this.draggingRespondent === DragFeature.TEXT) {
          this.$.textSelectionLayer.handleUpGesture();
        } else if (this.draggingRespondent === DragFeature.POST_SELECTION) {
          this.$.postSelectionRenderer.handleUpGesture();
        }
        break;
      case GestureState.STARTING:
        // This gesture was a tap. Let the features respond to a tap.
        if (this.draggingRespondent === DragFeature.TEXT) {
          this.$.textSelectionLayer.handleUpGesture();
          break;
        } else if (this.$.objectSelectionLayer.handleUpGesture(
                       this.currentGesture)) {
          break;
        }

        this.$.regionSelectionLayer.handleUpGesture(this.currentGesture);
        break;
      default:  // Other states are invalid and ignored.
        break;
    }

    // After features have responded to the event, reset the current drag state.
    this.currentGesture = emptyGestureEvent();
    this.draggingRespondent = DragFeature.NONE;
    this.removeDragListeners();
    this.resetCursor();
    this.dispatchEvent(new CustomEvent('pointer-released', {
      bubbles: true,
      composed: true,
    }));
  }

  private onPointerMove(event: PointerEvent) {
    // If a gesture hasn't started, ignore the pointer movement.
    if (this.currentGesture.state === GestureState.NOT_STARTED) {
      return;
    }

    this.updateGestureCoordinates(event);

    if (this.isDragging()) {
      this.set('currentGesture.state', GestureState.DRAGGING);

      // Capture pointer events so gestures still work if the users pointer
      // leaves the selection overlay div. Pointer capture is implicitly
      // released after pointerup or pointercancel events.
      this.setPointerCapture(event.pointerId);

      if (this.draggingRespondent === DragFeature.TEXT) {
        this.setCursorToText();
        this.$.textSelectionLayer.handleDragGesture(this.currentGesture);
      } else if (this.draggingRespondent === DragFeature.POST_SELECTION) {
        this.$.postSelectionRenderer.handleDragGesture(this.currentGesture);
      } else {
        // Let the features respond to the current drag if no other feature
        // responded first.
        this.setCursorToCrosshair();
        this.$.postSelectionRenderer.clearSelection();
        this.draggingRespondent = DragFeature.MANUAL_REGION;
        this.$.regionSelectionLayer.handleDragGesture(this.currentGesture);
      }
    }
  }

  private onPointerCancel() {
    // Pointer cancelled, so cancel any pending gestures.
    this.$.textSelectionLayer.cancelGesture();
    this.$.regionSelectionLayer.cancelGesture();
    this.$.postSelectionRenderer.cancelGesture();

    this.currentGesture = emptyGestureEvent();
    this.draggingRespondent = DragFeature.NONE;
    this.removeDragListeners();
    this.resetCursor();
  }

  private handleResize() {
    const newRect = this.getBoundingClientRect();

    if (this.initialHeight === 0 || this.initialWidth === 0) {
      this.initialWidth = newRect.width;
      this.initialHeight = newRect.height;
    }
    // We allow a buffer threshold when determining if the page has been
    // resized so that subtle one pixel adjustments don't trigger an entire
    // page reflow.
    this.isResized =
        Math.abs(newRect.height - this.initialHeight) >= RESIZE_THRESHOLD ||
        Math.abs(newRect.width - this.initialWidth) >= RESIZE_THRESHOLD;
    if (this.isResized) {
      this.isInitialSize = false;
      // The flash animation is cut short but animationend is never called if
      // the overlay is resized before animationend is called. This is because
      // the flash scrim is hidden on resize.
      this.onInitialFlashAnimationEnd();
    }
  }

  private handleSelectionElementsResize() {
    const selectionOverlayBounds =
        this.$.selectionOverlay.getBoundingClientRect();
    this.$.regionSelectionLayer.setCanvasSizeTo(
        selectionOverlayBounds.width, selectionOverlayBounds.height);
    this.$.objectSelectionLayer.setCanvasSizeTo(
        selectionOverlayBounds.width, selectionOverlayBounds.height);
    if (this.useShimmerCanvas) {
      this.$.overlayShimmerCanvas.setCanvasSizeTo(
          selectionOverlayBounds.width, selectionOverlayBounds.height);
    }
  }

  // Updates the currentGesture to correspond with the given PointerEvent.
  private updateGestureCoordinates(event: PointerEvent) {
    this.currentGesture.clientX = event.clientX;
    this.currentGesture.clientY = event.clientY;
  }

  // Returns if the given PointerEvent should be ignored.
  private shouldIgnoreEvent(event: PointerEvent) {
    if (this.isClosing) {
      return true;
    }
    const elementsAtPoint =
        this.shadowRoot!.elementsFromPoint(event.clientX, event.clientY);
    // Do not intercept events that should go to the following elements.
    if (elementsAtPoint.includes(this.$.selectedTextContextMenu) ||
        elementsAtPoint.includes(this.$.detectedTextContextMenu)) {
      return true;
    }
    // Ignore multi touch events and non-left/right click events.
    return !event.isPrimary || (event.button !== 0 && event.button !== 2);
  }

  // Returns whether the current gesture event is a drag.
  private isDragging() {
    if (this.currentGesture.state === GestureState.DRAGGING) {
      return true;
    }

    // TODO(b/329514345): Revisit if pointer movement is enough of an indicator,
    // or if we also need a timelimit on how long a tap can last before starting
    // a drag.
    const xMovement =
        Math.abs(this.currentGesture.clientX - this.currentGesture.startX);
    const yMovement =
        Math.abs(this.currentGesture.clientY - this.currentGesture.startY);
    return xMovement > DRAG_THRESHOLD || yMovement > DRAG_THRESHOLD;
  }

  private getContextMenuStyle(contextMenuX: number, contextMenuY: number):
      string {
    return `left: ${toPercent(contextMenuX)}; top: calc(${
        toPercent(contextMenuY)} + 12px)`;
  }

  private async handleCopy() {
    if (this.textSelectionStartIndex < 0 || this.textSelectionEndIndex < 0) {
      return;
    }
    this.browserProxy.handler.copyText(this.highlightedText);
    recordLensOverlayInteraction(INVOCATION_SOURCE, UserAction.kCopyText);
    this.dispatchEvent(new CustomEvent('text-copied', {
      bubbles: true,
      composed: true,
    }));
    this.showSelectedTextContextMenu = false;
  }

  private handleSelectText() {
    this.$.textSelectionLayer.selectAndSendWords(
        this.detectedTextStartIndex, this.detectedTextEndIndex);
    this.$.postSelectionRenderer.clearSelection();
    unfocusShimmer(this, ShimmerControlRequester.CURSOR);
  }

  private handleTranslate() {
    BrowserProxyImpl.getInstance().handler.issueTranslateSelectionRequest(
        this.highlightedText, this.contentLanguage,
        this.textSelectionStartIndex, this.textSelectionEndIndex);
    this.showSelectedTextContextMenu = false;
    recordLensOverlayInteraction(INVOCATION_SOURCE, UserAction.kTranslateText);
  }

  // Make the cursor disappear over the context menu, as if leaving the overlay.
  private handlePointerEnterContextMenu() {
    this.isPointerInside = false;
    this.isPointerInsideContextMenu = true;
    // Hide the cursor tooltip.
    this.dispatchEvent(
        new CustomEvent<CursorTooltipData>('set-cursor-tooltip', {
          bubbles: true,
          composed: true,
          detail: {tooltipType: CursorTooltipType.NONE},
        }));
    unfocusShimmer(this, ShimmerControlRequester.CURSOR);
  }

  private handlePointerLeaveContextMenu() {
    this.isPointerInside = true;
    this.isPointerInsideContextMenu = false;
    // Reshow the cursor tooltip.
    this.dispatchEvent(
        new CustomEvent<CursorTooltipData>('set-cursor-tooltip', {
          bubbles: true,
          composed: true,
          detail: {tooltipType: CursorTooltipType.REGION_SEARCH},
        }));
  }

  private onInitialFlashAnimationEnd() {
    if (this.hasInitialFlashAnimationEnded) {
      return;
    }
    this.hasInitialFlashAnimationEnded = true;
    this.eventTracker_.remove(this.$.initialFlashScrim, 'animationend');

    // Let the parent know the initial flash image animation has finished.
    this.dispatchEvent(new CustomEvent(
        'initial-flash-animation-end', {bubbles: true, composed: true}));

    // Don't start the shimmer animation until the initial flash animation is
    // finished.
    if (!this.disableShimmer) {
      if (this.useShimmerCanvas) {
        this.$.overlayShimmerCanvas.startAnimation();
      } else {
        this.$.overlayShimmer.startAnimation();
      }
    }
  }

  getShowDetectedTextContextMenuForTesting() {
    return this.showDetectedTextContextMenu;
  }

  getShowSelectedTextContextMenuForTesting() {
    return this.showSelectedTextContextMenu;
  }

  handleSelectTextForTesting() {
    this.handleSelectText();
  }

  handleTranslateForTesting() {
    this.handleTranslate();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'lens-selection-overlay': SelectionOverlayElement;
  }
}

customElements.define(SelectionOverlayElement.is, SelectionOverlayElement);
