# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import contextlib
from typing import TYPE_CHECKING, Iterator, Optional

from crossbench import plt
from crossbench.network.base import Network, TrafficShaper

if TYPE_CHECKING:
  from crossbench.path import LocalPath
  from crossbench.runner.groups.session import BrowserSessionRunGroup


class LocalFileNetwork(Network):

  def __init__(self,
               path: LocalPath,
               traffic_shaper: Optional[TrafficShaper] = None,
               browser_platform: plt.Platform = plt.PLATFORM):
    super().__init__(traffic_shaper, browser_platform)
    self._path = path


  @contextlib.contextmanager
  def open(self, session: BrowserSessionRunGroup) -> Iterator[Network]:
    with super().open(session):
      with self._open_local_file_server():
        with self._traffic_shaper.open(self, session):
          yield self

  @contextlib.contextmanager
  def _open_local_file_server(self):
    # TODO: implement
    yield

  def __str__(self) -> str:
    return f"LOCAL(path={self._path}, speed={self.traffic_shaper})"
