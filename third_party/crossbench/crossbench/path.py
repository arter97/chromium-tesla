# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import pathlib
from typing import Union

# A path that can refer to files on a remote platform with potentially
# a different Path flavour (e.g. Win vs Posix).
RemotePath = pathlib.PurePath

RemotePathLike = Union[str, RemotePath]

# A path that only ever refers to files on the local host / runner platform.
# Not that Path inherits from PurePath, and thus we can use a LocalPath in
# all places a RemotePath is expected.
LocalPath = pathlib.Path

LocalPathLike = Union[str, LocalPath]
