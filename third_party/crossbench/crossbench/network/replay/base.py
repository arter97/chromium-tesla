# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import contextlib
from typing import TYPE_CHECKING, Iterator, Optional

from crossbench import cli_helper, plt
from crossbench.network.base import Network, TrafficShaper
from crossbench.runner.groups.session import BrowserSessionRunGroup

if TYPE_CHECKING:
  from crossbench.path import LocalPath


class ReplayNetwork(Network):
  """ A network implementation that can be used to replay requests
  from a an archive."""

  def __init__(self,
               archive_path: LocalPath,
               traffic_shaper: Optional[TrafficShaper] = None,
               browser_platform: plt.Platform = plt.PLATFORM):
    super().__init__(traffic_shaper, browser_platform)
    self._archive_path = cli_helper.parse_existing_file_path(
        archive_path).resolve()

  @property
  def archive_path(self) -> LocalPath:
    return self._archive_path

  @contextlib.contextmanager
  def open(self, session: BrowserSessionRunGroup) -> Iterator[ReplayNetwork]:
    with super().open(session):
      with self._open_replay_sever(session):
        with self._traffic_shaper.open(self, session):
          yield self

  @contextlib.contextmanager
  def _open_replay_sever(self, session: BrowserSessionRunGroup):
    del session
    yield
