# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import re
from typing import Any, Optional

from crossbench.browsers.chromium.version import ChromiumVersion


class ChromeVersion(ChromiumVersion):

  _PREFIX_RE = re.compile(
      r"(?:google )?chr(?:ome)?[- ]?"
      r"(?:extended|stable|beta|dev|canary)?[- ]?m?", re.I)

  def _validate_prefix(self, prefix: Optional[str]) -> bool:
    if not prefix:
      return True
    prefix = prefix.lower()
    if prefix.strip() == "m":
      return True
    return bool(self._PREFIX_RE.fullmatch(prefix))

  def _validate_suffix(self, suffix: Optional[str]) -> bool:
    if suffix and "(Official Build)" in suffix:
      return True
    return super()._validate_suffix(suffix)
