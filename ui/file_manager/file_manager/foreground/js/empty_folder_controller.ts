// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type {VolumeInfo} from '../../background/js/volume_info.js';
import {queryRequiredElement} from '../../common/js/dom_utils.js';
import {getODFSMetadataQueryEntry, isInteractiveVolume, isOneDrive, isOneDrivePlaceholderKey, isRecentRootType} from '../../common/js/entry_utils.js';
import type {FakeEntry} from '../../common/js/files_app_entry_types.js';
import {isSkyvaultV2Enabled} from '../../common/js/flags.js';
import {str} from '../../common/js/translations.js';
import {FileErrorToDomError} from '../../common/js/util.js';
import {RootType} from '../../common/js/volume_manager_types.js';
import {myFilesEntryListKey, recentRootKey, updateIsInteractiveVolume} from '../../state/ducks/volumes.js';
import type {FileKey, State, Volume, VolumeId} from '../../state/state.js';
import {getStore} from '../../state/store.js';
import type {Store} from '../../state/store.js';

import {FSP_ACTION_HIDDEN_ONEDRIVE_REAUTHENTICATION_REQUIRED, ODFS_EXTENSION_ID} from './constants.js';
import type {DirectoryModel} from './directory_model.js';
import type {ProvidersModel} from './providers_model.js';

/**
 * The empty state image for the Recents folder.
 */
const RECENTS_EMPTY_FOLDER =
    'foreground/images/files/ui/empty_folder.svg#empty_folder';

/**
 * The image shown when search returned no results.
 */
const SEARCH_EMPTY_RESULTS =
    'foreground/images/files/ui/empty_search_results.svg#empty_search_results';

/**
 * The empty state image for the Trash folder.
 */
const TRASH_EMPTY_FOLDER =
    'foreground/images/files/ui/empty_trash_folder.svg#empty_trash_folder';

/**
 * The reauthentication required image for ODFS. There are no files when
 * reauthentication is required (scan fails).
 */
const ODFS_REAUTHENTICATION_REQUIRED = 'foreground/images/files/ui/' +
    'odfs_reauthentication_required.svg#odfs_reauthentication_required';

export type ScanFailedEvent = CustomEvent<{error: DOMError}>;

/**
 * Empty folder controller which controls the empty folder element inside
 * the file list container.
 */
export class EmptyFolderController {
  private image_: HTMLElement;
  private hasFilesystemError_: boolean = false;
  private store_: Store = getStore();
  private volumes_: (Record<VolumeId, Volume>)|null = null;
  private uiEntries_: FileKey[] = [];
  private localUserFilesAllowed_: boolean|undefined = true;
  protected isScanning_ = false;
  protected label_: HTMLElement;

  constructor(
      private emptyFolder_: HTMLElement,
      private directoryModel_: DirectoryModel,
      private providersModel_: ProvidersModel,
      private recentEntry_: FakeEntry) {
    this.label_ = queryRequiredElement('.label', this.emptyFolder_);
    this.image_ = queryRequiredElement('.image', this.emptyFolder_);
    this.store_.subscribe(this);

    this.directoryModel_.addEventListener(
        'cur-dir-scan-started', this.onScanStarted_.bind(this));
    this.directoryModel_.addEventListener(
        'cur-dir-scan-failed', this.onScanFailed_.bind(this));
    this.directoryModel_.addEventListener(
        'cur-dir-scan-canceled', this.onScanFinished.bind(this));
    this.directoryModel_.addEventListener(
        'cur-dir-scan-completed', this.onScanFinished.bind(this));
    this.directoryModel_.addEventListener(
        'cur-dir-rescan-completed', this.onScanFinished.bind(this));
  }

  /**
   * Checks whether the volumes or fake entries changed, and sets an error state
   * if there are no volumes except Recent to show.
   */
  async onStateChanged(state: State) {
    if (!isSkyvaultV2Enabled()) {
      return;
    }

    const localUserFilesAllowed = state.preferences?.localUserFilesAllowed;
    if (this.volumes_ !== state.volumes ||
        this.uiEntries_ !== state.uiEntries ||
        this.localUserFilesAllowed_ !== localUserFilesAllowed) {
      // MyFiles might still be present as an UI entry, in case the migration is
      // in process.
      const filteredUIEntries = state.uiEntries.filter(
          uiEntryKey => uiEntryKey !== myFilesEntryListKey &&
              uiEntryKey !== recentRootKey);
      if (Object.values(state.volumes).length === 0 &&
          filteredUIEntries.length === 0 && localUserFilesAllowed === false) {
        this.showFilesystemError_();
      } else {
        this.resetUi_(/*setImageVisible*/ true);
        this.hasFilesystemError_ = false;
      }
      this.volumes_ = state.volumes;
      this.uiEntries_ = state.uiEntries;
      this.localUserFilesAllowed_ = localUserFilesAllowed;
    }
  }

  /**
   * Handles scan start.
   */
  private onScanStarted_() {
    this.isScanning_ = true;
    this.updateUi_();
  }

  /**
   * Return true if reauthentication to OneDrive is required. Request the ODFS
   * volume metadata through the special root actions request to determine if re
   * authentication is required.
   */
  private async checkIfReauthenticationRequired_(odfsVolumeInfo: VolumeInfo):
      Promise<boolean> {
    // Request ODFS root actions to get ODFS metadata.
    return new Promise((fulfill) => {
      chrome.fileManagerPrivate.getCustomActions(
          [getODFSMetadataQueryEntry(odfsVolumeInfo) as DirectoryEntry],
          (customActions:
               chrome.fileManagerPrivate.FileSystemProviderAction[]) => {
            if (chrome.runtime.lastError) {
              console.error(
                  'Unexpectedly failed to fetch custom actions for ODFS ' +
                  'root because of: ' + chrome.runtime.lastError.message);
              fulfill(false);
              return;
            }
            // Find the reauthentication required action.
            for (const action of customActions) {
              if (action.id ===
                      FSP_ACTION_HIDDEN_ONEDRIVE_REAUTHENTICATION_REQUIRED &&
                  action.title === 'true') {
                fulfill(true);
                return;
              }
            }
            fulfill(false);
          });
    });
  }

  /**
   * Handles scan fail. If the scan failed for the ODFS volume due to
   * reauthenticaton being required, set the state of the volume as not
   * interactive.
   */
  protected onScanFailed_(event: ScanFailedEvent) {
    this.isScanning_ = false;
    const currentVolumeInfo = this.directoryModel_.getCurrentVolumeInfo();
    if (!currentVolumeInfo) {
      this.updateUi_();
      return;
    }
    // If scan did not fail for ODFS, return.
    if (!isOneDrive(currentVolumeInfo)) {
      this.updateUi_();
      return;
    }
    // If the error is not NO_MODIFICATION_ALLOWED_ERR, return. This is
    // equivalent to the ACCESS_DENIED error thrown by ODFS.
    if (event.detail.error.name !==
        FileErrorToDomError.NO_MODIFICATION_ALLOWED_ERR) {
      this.updateUi_();
      return;
    }
    // If ODFS is already non-interactive, return.
    if (!isInteractiveVolume(currentVolumeInfo)) {
      this.updateUi_();
      return;
    }
    // Only set ODFS to non-interactive if the ACCESS_DENIED was due to
    // reauthentication being required rather than some other access error.
    this.checkIfReauthenticationRequired_(currentVolumeInfo).then(required => {
      if (required) {
        // Set |isInteractive| to false for ODFS when reauthentication is
        // required.
        getStore().dispatch(updateIsInteractiveVolume({
          volumeId: currentVolumeInfo.volumeId,
          isInteractive: false,
        }));
      }
      this.updateUi_();
    });
  }

  /**
   * Handles scan finish.
   */
  onScanFinished() {
    const currentVolumeInfo = this.directoryModel_.getCurrentVolumeInfo();
    if (currentVolumeInfo && isOneDrive(currentVolumeInfo)) {
      if (!isInteractiveVolume(currentVolumeInfo)) {
        // Set |isInteractive| to true for ODFS when in an authenticated state.
        getStore().dispatch(updateIsInteractiveVolume({
          volumeId: currentVolumeInfo.volumeId,
          isInteractive: true,
        }));
      }
    }
    this.isScanning_ = false;
    this.updateUi_();
  }

  /**
   * Shows the given message. It may consist of just the `title`, or
   * `title` and `description`.
   */
  private showMessage_(title: string, description?: string) {
    if (!description) {
      this.label_.appendChild(document.createTextNode(title));
      return;
    }

    const titleSpan = document.createElement('span');
    titleSpan.id = 'empty-folder-title';
    titleSpan.innerText = title;
    const descSpan = document.createElement('span');
    descSpan.innerText = description;
    this.label_.appendChild(titleSpan);
    this.label_.appendChild(document.createElement('br'));
    this.label_.appendChild(descSpan);
  }

  /**
   * Shows the ODFS reauthentication required message. Include the "Sign in"
   * and "Settings" links and set the handlers.
   */
  private showOdfsReauthenticationMessage_() {
    const titleSpan = document.createElement('span');
    titleSpan.id = 'empty-folder-title';
    titleSpan.innerText = str('ONEDRIVE_LOGGED_OUT_TITLE');

    const text = document.createElement('span');
    text.innerText = str('ONEDRIVE_SIGN_IN_SUBTITLE');

    const signInLink = document.createElement('a');
    signInLink.setAttribute('class', 'sign-in');
    signInLink.innerText = str('ONEDRIVE_SIGN_IN_LINK');
    signInLink.addEventListener('click', this.onOdfsSignIn_.bind(this));

    const descSpan = document.createElement('span');
    descSpan.id = 'empty-folder-desc';
    descSpan.appendChild(text);
    descSpan.appendChild(document.createElement('br'));
    descSpan.appendChild(signInLink);

    this.label_.appendChild(titleSpan);
    this.label_.appendChild(document.createElement('br'));
    this.label_.appendChild(descSpan);
  }

  /**
   * Called when "Sign in" link for ODFS reauthentication is clicked. Request
   * a new ODFS mount. ODFS will unmount the old mount if the authentication is
   * successful in the new mount.
   */
  private onOdfsSignIn_() {
    const currentVolumeInfo = this.directoryModel_.getCurrentVolumeInfo();
    if (currentVolumeInfo && isOneDrive(currentVolumeInfo) &&
        currentVolumeInfo.providerId !== undefined) {
      this.providersModel_.requestMount(currentVolumeInfo.providerId);
    } else if (isOneDrivePlaceholderKey(
                   this.directoryModel_.getCurrentFileKey())) {
      this.providersModel_.requestMount(ODFS_EXTENSION_ID);
    }
  }

  /**
   * Shows the message informing the user that the file system isn't available,
   * for example because the admin misconfigured SkyVault policies.
   */
  private showFilesystemError_() {
    if (!isSkyvaultV2Enabled()) {
      return;
    }
    this.resetUi_(/*setImageVisible*/ false);

    this.image_.hidden = true;
    this.emptyFolder_.hidden = false;
    // TODO(b/334511998): Use proper strings.
    this.showMessage_(
        'File system has been disabled. Please contact your administrator.');
    this.label_.classList.add('no-image');

    this.hasFilesystemError_ = true;
  }

  /**
   * Resets the empty folder UI.
   * @param setImageVisible Whether the image element should be visible.
   */
  private resetUi_(setImageVisible: boolean) {
    this.label_.innerText = '';
    this.emptyFolder_.hidden = true;

    if (setImageVisible) {
      this.image_.hidden = false;
      this.label_.classList.remove('no-image');
    } else {
      this.image_.hidden = true;
      this.label_.classList.add('no-image');
    }
  }

  /**
   * Updates visibility of empty folder UI.
   */
  protected updateUi_() {
    if (this.hasFilesystemError_) {
      return;
    }
    const currentRootType = this.directoryModel_.getCurrentRootType();
    const currentVolumeInfo = this.directoryModel_.getCurrentVolumeInfo();

    let svgRef = null;
    if (isRecentRootType(currentRootType)) {
      svgRef = RECENTS_EMPTY_FOLDER;
    } else if (currentRootType === RootType.TRASH) {
      svgRef = TRASH_EMPTY_FOLDER;
    } else if (
        currentVolumeInfo && isOneDrive(currentVolumeInfo) &&
        !isInteractiveVolume(currentVolumeInfo)) {
      // Show ODFS reauthentication required empty state if is it
      // non-interactive.
      svgRef = ODFS_REAUTHENTICATION_REQUIRED;
    } else if (isOneDrivePlaceholderKey(
                   this.directoryModel_.getCurrentFileKey())) {
      svgRef = ODFS_REAUTHENTICATION_REQUIRED;
    } else {
      const {search} = getStore().getState();
      if (search && search.query) {
        svgRef = SEARCH_EMPTY_RESULTS;
      }
    }

    const fileListModel = this.directoryModel_.getFileList();

    this.resetUi_(/*setImageVisible*/ true);
    if (svgRef === null || this.isScanning_ ||
        (fileListModel && fileListModel.length > 0)) {
      return;
    }

    const svgUseElement =
        this.image_.querySelector<SVGUseElement>('.image > svg > use')!;
    svgUseElement.setAttributeNS(
        'http://www.w3.org/1999/xlink', 'xlink:href', svgRef);
    this.emptyFolder_.hidden = false;

    if (svgRef === TRASH_EMPTY_FOLDER) {
      this.showMessage_(
          str('EMPTY_TRASH_FOLDER_TITLE'), str('EMPTY_TRASH_FOLDER_DESC'));
      return;
    }

    if (svgRef === ODFS_REAUTHENTICATION_REQUIRED) {
      this.showOdfsReauthenticationMessage_();
      return;
    }

    if (svgRef === SEARCH_EMPTY_RESULTS) {
      this.showMessage_(
          str('SEARCH_NO_MATCHING_RESULTS_TITLE'),
          str('SEARCH_NO_MATCHING_RESULTS_DESC'));
      return;
    }

    switch (this.recentEntry_.fileCategory) {
      case chrome.fileManagerPrivate.FileCategory.AUDIO:
        this.showMessage_(str('RECENT_EMPTY_AUDIO_FOLDER'));
        break;
      case chrome.fileManagerPrivate.FileCategory.DOCUMENT:
        this.showMessage_(str('RECENT_EMPTY_DOCUMENTS_FOLDER'));
        break;
      case chrome.fileManagerPrivate.FileCategory.IMAGE:
        this.showMessage_(str('RECENT_EMPTY_IMAGES_FOLDER'));
        break;
      case chrome.fileManagerPrivate.FileCategory.VIDEO:
        this.showMessage_(str('RECENT_EMPTY_VIDEOS_FOLDER'));
        break;
      default:
        this.showMessage_(str('RECENT_EMPTY_FOLDER'));
    }
  }
}
