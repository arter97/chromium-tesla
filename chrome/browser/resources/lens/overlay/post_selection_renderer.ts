// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {assert, assertNotReached} from '//resources/js/assert.js';
import {EventTracker} from '//resources/js/event_tracker.js';
import {PolymerElement} from '//resources/polymer/v3_0/polymer/polymer_bundled.min.js';

import {BrowserProxyImpl} from './browser_proxy.js';
import type {BrowserProxy} from './browser_proxy.js';
import {CenterRotatedBox_CoordinateType} from './geometry.mojom-webui.js';
import type {CenterRotatedBox} from './geometry.mojom-webui.js';
import {UserAction} from './lens.mojom-webui.js';
import {INVOCATION_SOURCE} from './lens_overlay_app.js';
import {recordLensOverlayInteraction} from './metrics_utils.js';
import {getTemplate} from './post_selection_renderer.html.js';
import {focusShimmerOnRegion, ShimmerControlRequester, unfocusShimmer} from './selection_utils.js';
import type {GestureEvent} from './selection_utils.js';
import {toPercent, toPixels} from './values_converter.js';

// Bounding box send to PostSelectionRendererElement to render a bounding box.
// The numbers should be normalized to the image dimensions, between 0 and 1
export interface PostSelectionBoundingBox {
  top: number;
  left: number;
  width: number;
  height: number;
}

// The target currently being dragged on by the user.
enum DragTarget {
  NONE,
  TOP_LEFT,
  TOP_RIGHT,
  BOTTOM_RIGHT,
  BOTTOM_LEFT,
}

// The amount of pixels around the edge leave as a buffer so user can't drag too
// far. Exported for testing.
export const PERIMETER_SELECTION_PADDING_PX = 4;
// The maximum length of a corner. Exported for testing.
export const MAX_CORNER_LENGTH_PX = 22;
// The maximum radius of a corner. Exported for testing.
export const MAX_CORNER_RADIUS_PX = 14;
// Cutout radius used with larger corner radii. Exported for testing.
export const CUTOUT_RADIUS_PX = 5;
// A cutout radius will only be used when the corner radius is above this
// threshold.
const CUTOUT_RADIUS_THRESHOLD_PX = 12;
// Minimum box size allowed. Exported for testing.
export const MIN_BOX_SIZE_PX = 12;

function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), max);
}

export interface PostSelectionRendererElement {
  $: {
    postSelection: HTMLElement,
  };
}

interface CornerDimensions {
  length: number;
  radius: number;
  cutoutRadius: number;
}

/*
 * Renders the users visual selection after one is made. This element is also
 * responsible for allowing the user to adjust their region to issue a new
 * Lens request.
 */
export class PostSelectionRendererElement extends PolymerElement {
  static get is() {
    return 'post-selection-renderer';
  }

  static get template() {
    return getTemplate();
  }

  static get properties() {
    return {
      top: Number,
      left: Number,
      height: Number,
      width: Number,
      screenshotDataUri: String,
      currentDragTarget: Number,
      cornerIds: Array,
    };
  }

  private eventTracker_: EventTracker = new EventTracker();
  // The bounds of the current selection
  private top: number = 0;
  private left: number = 0;
  private height: number = 0;
  private width: number = 0;
  // The data URI of the current overlay screenshot.
  private screenshotDataUri: string;
  // What is currently being dragged by the user.
  private currentDragTarget: DragTarget = DragTarget.NONE;
  // IDs used to generate the corner hitbox divs.
  private cornerIds: string[] =
      ['topLeft', 'topRight', 'bottomRight', 'bottomLeft'];
  // Listener IDs for events tracked from the browser.
  private listenerIds: number[];
  // The original bounds from the start of a drag.
  private originalBounds:
      PostSelectionBoundingBox = {left: 0, top: 0, width: 0, height: 0};
  private browserProxy: BrowserProxy = BrowserProxyImpl.getInstance();
  private resizeObserver: ResizeObserver = new ResizeObserver(() => {
    this.handleResize();
  });
  private newBoxAnimation: Animation|null = null;
  private animateOnResize = false;

  override connectedCallback() {
    super.connectedCallback();
    this.eventTracker_.add(
        document, 'render-post-selection',
        (e: CustomEvent<PostSelectionBoundingBox>) => {
          this.onRenderPostSelection(e);
        });
    this.resizeObserver.observe(this);
    // Set up listener to listen to events from C++.
    this.listenerIds = [
      this.browserProxy.callbackRouter.clearAllSelections.addListener(
          this.clearSelection.bind(this)),
      this.browserProxy.callbackRouter.clearRegionSelection.addListener(
          this.clearSelection.bind(this)),
      this.browserProxy.callbackRouter.setPostRegionSelection.addListener(
          this.setSelection.bind(this)),
    ];
  }

  override disconnectedCallback() {
    super.disconnectedCallback();
    this.eventTracker_.removeAll();
    this.resizeObserver.unobserve(this);
    this.listenerIds.forEach(
        id => assert(this.browserProxy.callbackRouter.removeListener(id)));
    this.listenerIds = [];
  }

  clearSelection() {
    unfocusShimmer(this, ShimmerControlRequester.POST_SELECTION);
    this.height = 0;
    this.width = 0;
    this.dispatchEvent(new CustomEvent(
        'hide-detected-text-context-menu', {bubbles: true, composed: true}));
    this.notifyPostSelectionUpdated();
  }

  handleDownGesture(event: GestureEvent): boolean {
    this.currentDragTarget =
        this.dragTargetFromPoint(event.clientX, event.clientY);

    if (this.shouldHandleDownGesture()) {
      // User is dragging the post selection (if enabled) or resizing.
      this.originalBounds = {
        left: this.left,
        top: this.top,
        width: this.width,
        height: this.height,
      };
      return true;
    }
    return false;
  }

  handleDragGesture(event: GestureEvent) {
    const imageBounds = this.getBoundingClientRect();
    const normalizedX = (event.clientX - imageBounds.left) / imageBounds.width;
    const normalizedY = (event.clientY - imageBounds.top) / imageBounds.height;
    const normalizedMinBoxWidth = MIN_BOX_SIZE_PX / imageBounds.width;
    const normalizedMinBoxHeight = MIN_BOX_SIZE_PX / imageBounds.height;
    const normalizedPerimeterPaddingWidth =
        PERIMETER_SELECTION_PADDING_PX / imageBounds.width;
    const normalizedPerimeterPaddingHeight =
        PERIMETER_SELECTION_PADDING_PX / imageBounds.height;
    const minXValue = normalizedPerimeterPaddingWidth;
    const minYValue = normalizedPerimeterPaddingHeight;
    const maxXValue = 1 - normalizedPerimeterPaddingWidth;
    const maxYValue = 1 - normalizedPerimeterPaddingHeight;

    const currentLeft = this.left;
    const currentTop = this.top;
    const currentRight = this.left + this.width;
    const currentBottom = this.top + this.height;
    let newLeft;
    let newTop;
    let newRight;
    let newBottom;

    switch (this.currentDragTarget) {
      case DragTarget.TOP_LEFT:
        newLeft = Math.min(normalizedX, currentRight - normalizedMinBoxWidth);
        newTop = Math.min(normalizedY, currentBottom - normalizedMinBoxHeight);
        newRight = currentRight;
        newBottom = currentBottom;
        break;
      case DragTarget.TOP_RIGHT:
        newLeft = currentLeft;
        newTop = Math.min(normalizedY, currentBottom - normalizedMinBoxHeight);
        newRight = Math.max(normalizedX, currentLeft + normalizedMinBoxWidth);
        newBottom = currentBottom;
        break;
      case DragTarget.BOTTOM_RIGHT:
        newLeft = currentLeft;
        newTop = currentTop;
        newRight = Math.max(normalizedX, currentLeft + normalizedMinBoxWidth);
        newBottom = Math.max(normalizedY, currentTop + normalizedMinBoxHeight);
        break;
      case DragTarget.BOTTOM_LEFT:
        newLeft = Math.min(normalizedX, currentRight - normalizedMinBoxWidth);
        newTop = currentTop;
        newRight = currentRight;
        newBottom = Math.max(normalizedY, currentTop + normalizedMinBoxHeight);
        break;
      default:
        assertNotReached();
    }
    assert(newLeft !== undefined);
    assert(newTop !== undefined);
    assert(newRight !== undefined);
    assert(newBottom !== undefined);

    // Ensure the new region is within the image bounds.
    newLeft = clamp(newLeft, minXValue, maxXValue - normalizedMinBoxWidth);
    newTop = clamp(newTop, minYValue, maxYValue - normalizedMinBoxHeight);
    newRight = clamp(newRight, minXValue + normalizedMinBoxWidth, maxXValue);
    newBottom = clamp(newBottom, minYValue + normalizedMinBoxHeight, maxYValue);

    // Set the new dimensions.
    this.left = newLeft;
    this.top = newTop;
    this.width = newRight - newLeft;
    this.height = newBottom - newTop;

    this.rerender();
  }

  handleUpGesture() {
    if (this.areBoundsChanging()) {
      // Issue Lens request for new bounds
      BrowserProxyImpl.getInstance().handler.issueLensRegionRequest(
          this.getNormalizedCenterRotatedBox(), /*is_click=*/ false);

      // Check for selectable text
      this.dispatchEvent(new CustomEvent('detect-text-in-region', {
        bubbles: true,
        composed: true,
        detail: this.getNormalizedCenterRotatedBox(),
      }));

      recordLensOverlayInteraction(
          INVOCATION_SOURCE, UserAction.kRegionSelectionChange);
    }

    this.originalBounds = {left: 0, top: 0, width: 0, height: 0};
    this.currentDragTarget = DragTarget.NONE;
  }

  cancelGesture() {
    this.originalBounds = {left: 0, top: 0, width: 0, height: 0};
    this.currentDragTarget = DragTarget.NONE;
  }

  private setSelection(region: CenterRotatedBox) {
    const normalizedTop = region.box.y - (region.box.height / 2);
    const normalizedLeft = region.box.x - (region.box.width / 2);

    this.top = normalizedTop;
    this.left = normalizedLeft;
    this.height = region.box.height;
    this.width = region.box.width;
    this.originalBounds = {left: 0, top: 0, width: 0, height: 0};

    this.rerender();
    this.triggerNewBoxAnimation();
  }

  private onRenderPostSelection(e: CustomEvent<PostSelectionBoundingBox>) {
    this.top = e.detail.top;
    this.left = e.detail.left;
    this.height = e.detail.height;
    this.width = e.detail.width;

    this.rerender();
    this.triggerNewBoxAnimation();
  }

  private handleResize() {
    // Only update properties defined absolutely, i.e. corner dimensions.
    // Properties that are defined relatively do not need to be updated.
    this.updateCornerDimensions();
    if (this.newBoxAnimation) {
      (this.newBoxAnimation.effect as KeyframeEffect)
          .setKeyframes(this.getNewBoxAnimationKeyframes());
    } else if (this.animateOnResize) {
      this.animateOnResize = false;
      this.triggerNewBoxAnimation();
    }
  }

  private rerender() {
    // Set the CSS properties to reflect current bounds and force rerender.
    this.style.setProperty('--selection-width', toPercent(this.width));
    this.style.setProperty('--selection-height', toPercent(this.height));
    this.style.setProperty('--selection-top', toPercent(this.top));
    this.style.setProperty('--selection-left', toPercent(this.left));

    this.updateCornerDimensions();

    // Focus the shimmer on the new post selection region.
    focusShimmerOnRegion(
        this, this.top, this.left, this.width, this.height,
        ShimmerControlRequester.POST_SELECTION);

    this.notifyPostSelectionUpdated();
  }

  private updateCornerDimensions() {
    const cornerDimensions = this.getCornerDimensions();
    this.style.setProperty(
        '--post-selection-corner-horizontal-length',
        toPixels(cornerDimensions.length));
    this.style.setProperty(
        '--post-selection-corner-vertical-length',
        toPixels(cornerDimensions.length));
    this.style.setProperty(
        '--post-selection-corner-radius', toPixels(cornerDimensions.radius));
    this.style.setProperty(
        '--post-selection-cutout-corner-radius',
        toPixels(cornerDimensions.cutoutRadius));
  }

  private triggerNewBoxAnimation() {
    const parentBoundingRect = this.getBoundingClientRect();
    if (parentBoundingRect.width === 0 || parentBoundingRect.height === 0) {
      // Renderer has probably not been sized yet. Defer until resize.
      this.animateOnResize = true;
      return;
    }

    this.newBoxAnimation = this.animate(this.getNewBoxAnimationKeyframes(), {
      duration: 450,
      easing: 'cubic-bezier(0.2, 0.0, 0, 1.0)',
    });
    this.newBoxAnimation.onfinish = () => {
      this.newBoxAnimation = null;
    };
  }

  private getNewBoxAnimationKeyframes() {
    const parentBoundingRect = this.getBoundingClientRect();
    const cornerDimensions = this.getCornerDimensions();
    return [
      {
        [`--post-selection-corner-horizontal-length`]:
            toPixels(parentBoundingRect.width * this.width / 2),
        [`--post-selection-corner-vertical-length`]:
            toPixels(parentBoundingRect.height * this.height / 2),
      },
      {
        [`--post-selection-corner-horizontal-length`]:
            toPixels(cornerDimensions.length),
        [`--post-selection-corner-vertical-length`]:
            toPixels(cornerDimensions.length),
      },
    ];
  }

  private getCornerDimensions(): CornerDimensions {
    const imageBounds = this.getBoundingClientRect();
    if (imageBounds.width === 0 || imageBounds.height === 0) {
      // Renderer has probably not been sized yet. Return default values.
      return {
        length: MAX_CORNER_LENGTH_PX,
        radius: MAX_CORNER_RADIUS_PX,
        cutoutRadius: CUTOUT_RADIUS_PX,
      };
    }

    const shortestSide = Math.min(
        this.width * imageBounds.width, this.height * imageBounds.height);
    const length = Math.min(shortestSide / 2, MAX_CORNER_LENGTH_PX);
    const radius = Math.min(shortestSide / 3, MAX_CORNER_RADIUS_PX);
    // Do not use a cutout radius at small radii to prevent gaps.
    const cutoutRadius =
        radius > CUTOUT_RADIUS_THRESHOLD_PX ? CUTOUT_RADIUS_PX : 0;

    return {length, radius, cutoutRadius};
  }

  private notifyPostSelectionUpdated() {
    this.dispatchEvent(new CustomEvent('post-selection-updated', {
      bubbles: true,
      composed: true,
      detail: {
        top: this.top,
        left: this.left,
        width: this.width,
        height: this.height,
      },
    }));
  }

  // Returns if the current bounds are being updated.
  private areBoundsChanging() {
    return this.originalBounds.top !== this.top ||
        this.originalBounds.left !== this.left ||
        this.originalBounds.height !== this.height ||
        this.originalBounds.width !== this.width;
  }

  /**
   * @return Returns the drag target at the given point.
   */
  private dragTargetFromPoint(x: number, y: number): DragTarget {
    const topMostElements = this.shadowRoot!.elementsFromPoint(x, y);
    const topMostDraggableElement = topMostElements.find(el => {
      return (el instanceof HTMLElement) &&
          el.classList.contains('corner-hit-box');
    });
    if (!topMostDraggableElement) {
      return DragTarget.NONE;
    }
    switch (topMostDraggableElement.id) {
      case 'topLeft':
        return DragTarget.TOP_LEFT;
      case 'topRight':
        return DragTarget.TOP_RIGHT;
      case 'bottomRight':
        return DragTarget.BOTTOM_RIGHT;
      case 'bottomLeft':
        return DragTarget.BOTTOM_LEFT;
      default:
        // Did not click on a target we care about.
        break;
    }
    return DragTarget.NONE;
  }

  private shouldHandleDownGesture(): boolean {
    return this.currentDragTarget !== DragTarget.NONE;
  }

  // Converts the current region to a CenterRotatedBox
  private getNormalizedCenterRotatedBox(): CenterRotatedBox {
    return {
      box: {
        x: this.left + (this.width / 2),
        y: this.top + (this.height / 2),
        width: this.width,
        height: this.height,
      },
      rotation: 0,
      coordinateType: CenterRotatedBox_CoordinateType.kNormalized,
    };
  }

  private getScrimStyleProperties() {
    // If there is no selection, set opacity to zero to trigger fade out
    // CSS transition.
    return !this.hasSelection() ? 'opacity: 0;' : '';
  }

  // Used in HTML template to know if there is currently a selection to render.
  private hasSelection(): boolean {
    return this.width > 0 && this.height > 0;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'post-selection-renderer': PostSelectionRendererElement;
  }
}

customElements.define(
    PostSelectionRendererElement.is, PostSelectionRendererElement);

// Setup CSS Houdini API
CSS.paintWorklet.addModule('post_selection_paint_worklet.js');

// Variables controlling the rendered post selection
CSS.registerProperty({
  name: '--post-selection-corner-horizontal-length',
  syntax: '<length>',
  inherits: true,
  initialValue: '22px',
});
CSS.registerProperty({
  name: '--post-selection-corner-vertical-length',
  syntax: '<length>',
  inherits: true,
  initialValue: '22px',
});
CSS.registerProperty({
  name: '--post-selection-corner-width',
  syntax: '<length>',
  inherits: true,
  initialValue: '4px',
});
CSS.registerProperty({
  name: '--post-selection-corner-radius',
  syntax: '<length>',
  inherits: true,
  initialValue: '14px',
});
