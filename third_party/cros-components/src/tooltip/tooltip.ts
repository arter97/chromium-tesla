/**
 * @license
 * Copyright 2023 Google LLC
 * SPDX-License-Identifier: Apache-2.0
 */

import {css, CSSResultGroup, html, LitElement} from 'lit';

/**
 * Allow popover show and hide functions. See
 * https://developer.mozilla.org/en-US/docs/Web/API/Popover_API
 * go/typescript-reopening
 */
declare interface HTMLElementWithPopoverAPI extends HTMLElement {
  showPopover: () => void;
  hidePopover: () => void;
}

// The name of the anchor to use for the CSS Anchor Positioning API.
const ANCHOR_NAME = css`--tooltip-anchor`;

// The amount of time to wait before closing the tooltip, after the user stopped
// focusing/hovering the anchor or label.
/* TODO(b/288190663): Confirm correct timeout duration value. **/
const BLUR_OR_UNFOCUS_TIMEOUT_DURATION = 400;

/** A cros-compliant tooltip component. */
export class Tooltip extends LitElement {
  /** @nocollapse */
  static override styles: CSSResultGroup = css`
    /* Remove default CSS Popover API styles. */
    :host {
      border: 0;
      margin: 0;
      padding: 0;
    }

    :host(:not(:popover-open)) {
      opacity: 0;
      /* TODO(b/288190663): Update exit animation to match SystemUI. **/
      transition: all 400ms allow-discrete;
    }

    :host(:popover-open) {
      opacity: 1;
      /* TODO(b/288190663): Update entrance animation to match SystemUI. **/
      transition: all 300ms allow-discrete;
    }

    @starting-style {
      :host(:popover-open) {
        opacity: 0;
      }
    }

    #tooltip-anchor-overlay {
      anchor-name: ${ANCHOR_NAME};
      opacity: 0;
      pointer-events: none;
      position: fixed;
    }

    #label {
      background-color: var(--cros-sys-inverse_surface);
      border-radius: 6px;
      color: var(--cros-sys-surface);
      font: var(--cros-annotation-1-font);
      inset-area: block-end;
      inset-block-start: 4px;
      padding: 5px 8px;
      position-anchor: ${ANCHOR_NAME};
      position: fixed;
      text-align: center;
    }
  `;
  /** @nocollapse */
  static override properties = {
    anchor: {type: String},
    label: {type: String},
  };

  /** @export */
  anchor: string;
  /** @export */
  label: string;

  constructor() {
    super();
    this.anchorElement = null;
    this.anchor = '';
    this.label = '';

    this.anchorOrLabelFocused = this.anchorOrLabelFocused.bind(this);
    this.anchorOrLabelBlurred = this.anchorOrLabelBlurred.bind(this);
  }

  /** @export */
  get anchorElement(): HTMLElement|null {
    if (this.anchor) {
      return (this.getRootNode() as Document | ShadowRoot)
          .querySelector(`#${this.anchor}`);
    }
    return this.currentAnchorElement;
  }

  /** @export */
  set anchorElement(element: HTMLElement|null) {
    this.currentAnchorElement = element;
    this.requestUpdate('anchorElement');
  }

  private currentAnchorElement: HTMLElement|null = null;
  private isAnchorOrLabelFocused = false;
  private blurOrUnfocusTimeout: number|null = null;

  override firstUpdated() {
    // Adds needed popover properties to cros-tooltip.
    this.setAttribute('popover', 'auto');
    this.setAttribute('id', 'tooltip');

    // Sets anchor to this element's ID & adds listeners on hover.
    if (this.anchorElement) {
      this.anchorElement.setAttribute('popovertarget', 'tooltip');
      this.anchorElement.addEventListener(
          'mouseover', this.anchorOrLabelFocused);
      this.anchorElement.addEventListener('focus', this.anchorOrLabelFocused);
      this.anchorElement.addEventListener(
          'mouseout', this.anchorOrLabelBlurred);
      this.anchorElement.addEventListener('blur', this.anchorOrLabelBlurred);
    }
  }

  override disconnectedCallback() {
    if (this.anchorElement) {
      this.anchorElement.removeEventListener(
          'mouseover', this.anchorOrLabelFocused);
      this.anchorElement.removeEventListener(
          'focus', this.anchorOrLabelFocused);
      this.anchorElement.removeEventListener(
          'mouseout', this.anchorOrLabelBlurred);
      this.anchorElement.removeEventListener('blur', this.anchorOrLabelBlurred);
    }
    super.disconnectedCallback();
  }

  get labelElement(): HTMLDivElement {
    return this.shadowRoot!.querySelector<HTMLDivElement>('#label')!;
  }

  openPopover() {
    // When the popover opens, move the tooltip to the anchor.
    this.updateAnchorPosition();

    const tooltip = this as unknown as HTMLElementWithPopoverAPI;
    tooltip.showPopover();
  }

  closePopover() {
    const tooltip = this as unknown as HTMLElementWithPopoverAPI;
    tooltip.hidePopover();
  }

  private anchorOrLabelFocused() {
    this.isAnchorOrLabelFocused = true;
    this.openPopover();
  }

  private anchorOrLabelBlurred() {
    this.isAnchorOrLabelFocused = false;
    this.maybeCloseTooltipAfterTimeout();
  }

  private maybeCloseTooltipAfterTimeout() {
    // Restart the timeout if one already exists.
    if (this.blurOrUnfocusTimeout) {
      clearTimeout(this.blurOrUnfocusTimeout);
    }

    // Close the tooltip after a set amount of time, but don't close the tooltip
    // if the user is still focusing the label or the anchor.
    this.blurOrUnfocusTimeout = setTimeout(() => {
      if (!this.isAnchorOrLabelFocused) {
        this.closePopover();
      }
    }, BLUR_OR_UNFOCUS_TIMEOUT_DURATION);
  }

  // The CSS Anchor Positioning API does not yet allow elements in a shadow DOM
  // to see anchor names defined in the outer tree scope. To work around this,
  // there is an internal anchor point (#tooltip-anchor-overlay) that we
  // position to overlap exactly with the actual anchor element. From there, we
  // can use the CSS Anchor Positioning API to position the tooltip. See
  // https://github.com/w3c/csswg-drafts/issues/9408#issuecomment-2105453734 for
  // more context.
  private updateAnchorPosition() {
    if (!this.anchorElement) return;

    const anchorOverlay =
        this.shadowRoot!.getElementById('tooltip-anchor-overlay')!;
    const anchorRect = this.anchorElement.getBoundingClientRect();

    // Move the anchor overlay so that it overlaps the anchor element.
    anchorOverlay.style.top = `${anchorRect.top}px`;
    anchorOverlay.style.left = `${anchorRect.left}px`;
    anchorOverlay.style.width = `${anchorRect.width}px`;
    anchorOverlay.style.height = `${anchorRect.height}px`;
  }

  override render() {
    return html`
      <div id="tooltip-anchor-overlay">
      </div>
      <div id="label"
        @mouseover=${this.anchorOrLabelFocused}
        @mouseout=${this.anchorOrLabelBlurred}
        @focus=${this.anchorOrLabelFocused}
        @blur=${this.anchorOrLabelBlurred}
      >${this.label}</div>
    `;
  }
}

customElements.define('cros-tooltip', Tooltip);

declare global {
  interface HTMLElementTagNameMap {
    'cros-tooltip': Tooltip;
  }
}
