# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from __future__ import annotations

import abc
import logging
from typing import TYPE_CHECKING, Any, Dict, Iterable, List, Optional, cast

from crossbench import path as pth
from crossbench.probes.helper import INTERNAL_NAME_PREFIX

if TYPE_CHECKING:
  from crossbench.probes.probe import Probe
  from crossbench.runner.result_origin import ResultOrigin
  from crossbench.types import JsonDict


class ProbeResult(abc.ABC):

  def __init__(self,
               url: Optional[Iterable[str]] = None,
               file: Optional[Iterable[pth.LocalPath]] = None,
               json: Optional[Iterable[pth.LocalPath]] = None,
               csv: Optional[Iterable[pth.LocalPath]] = None):
    self._url_list = tuple(url) if url else ()
    self._file_list = tuple(file) if file else ()
    self._json_list = tuple(json) if json else ()
    self._csv_list = tuple(csv) if csv else ()
    # TODO: Add Metric object for keeping metrics in-memory instead of reloading
    # them from serialized JSON files for merging.
    self._values = None
    self._validate()

  @property
  def is_empty(self) -> bool:
    return not any(
        (self._url_list, self._file_list, self._json_list, self._csv_list))

  @property
  def is_remote(self) -> bool:
    return False

  def __bool__(self) -> bool:
    return not self.is_empty

  def __eq__(self, other: Any) -> bool:
    if not isinstance(other, ProbeResult):
      return False
    if self is other:
      return True
    if self.is_empty and other.is_empty:
      return True
    if self._file_list != other._file_list:
      return False
    if self._json_list != other._json_list:
      return False
    if self._csv_list != other._csv_list:
      return False
    return self._url_list == other._url_list

  def merge(self, other: ProbeResult) -> ProbeResult:
    if self.is_empty:
      return other
    if other.is_empty:
      return self
    return LocalProbeResult(
        url=self.url_list + other.url_list,
        file=self.file_list + other.file_list,
        json=self.json_list + other.json_list,
        csv=self.csv_list + other.csv_list)

  def _validate(self) -> None:
    for path in self._file_list:
      if path.suffix in (".csv", ".json"):
        raise ValueError(f"Use specific parameter for result: {path}")
    for path in self._json_list:
      if path.suffix != ".json":
        raise ValueError(f"Expected .json file but got: {path}")
    for path in self._csv_list:
      if path.suffix != ".csv":
        raise ValueError(f"Expected .csv file but got: {path}")
    for path in self.all_files():
      if not path.exists():
        raise ValueError(f"ProbeResult path does not exist: {path}")

  def to_json(self) -> JsonDict:
    result: JsonDict = {}
    if self._url_list:
      result["url"] = self._url_list
    if self._file_list:
      result["file"] = list(map(str, self._file_list))
    if self._json_list:
      result["json"] = list(map(str, self._json_list))
    if self._csv_list:
      result["csv"] = list(map(str, self._csv_list))
    return result

  @property
  def has_files(self) -> bool:
    return (bool(self._file_list) or bool(self._json_list) or
            bool(self._csv_list))

  def all_files(self) -> Iterable[pth.LocalPath]:
    yield from self._file_list
    yield from self._json_list
    yield from self._csv_list

  @property
  def url(self) -> str:
    assert len(self._url_list) == 1
    return self._url_list[0]

  @property
  def url_list(self) -> List[str]:
    return list(self._url_list)

  @property
  def file(self) -> pth.LocalPath:
    assert len(self._file_list) == 1
    return self._file_list[0]

  @property
  def file_list(self) -> List[pth.LocalPath]:
    return list(self._file_list)

  @property
  def json(self) -> pth.LocalPath:
    assert len(self._json_list) == 1
    return self._json_list[0]

  @property
  def json_list(self) -> List[pth.LocalPath]:
    return list(self._json_list)

  @property
  def csv(self) -> pth.LocalPath:
    assert len(self._csv_list) == 1
    return self._csv_list[0]

  @property
  def csv_list(self) -> List[pth.LocalPath]:
    return list(self._csv_list)


class EmptyProbeResult(ProbeResult):

  def __init__(self) -> None:
    super().__init__()

  def __bool__(self) -> bool:
    return False


class LocalProbeResult(ProbeResult):
  """LocalProbeResult can be used for files that are always available on the
  runner/local machine."""

  def __init__(self,
               url: Optional[Iterable[str]] = None,
               file: Optional[Iterable[pth.LocalPath]] = None,
               json: Optional[Iterable[pth.LocalPath]] = None,
               csv: Optional[Iterable[pth.LocalPath]] = None):
    super().__init__(url, file, json, csv)


class BrowserProbeResult(ProbeResult):
  """BrowserProbeResult are stored on the device where the browser runs.
  Result files will be automatically transferred to the local run's results
  folder.
  """

  def __init__(self,
               result_origin: ResultOrigin,
               url: Optional[Iterable[str]] = None,
               file: Optional[Iterable[pth.RemotePath]] = None,
               json: Optional[Iterable[pth.RemotePath]] = None,
               csv: Optional[Iterable[pth.RemotePath]] = None):
    self._browser_file = file
    self._browser_json = json
    self._browser_csv = csv
    self._is_remote = result_origin.is_remote
    if self._is_remote:
      local_file = self._copy_files(result_origin, file)
      local_json = self._copy_files(result_origin, json)
      local_csv = self._copy_files(result_origin, csv)
    else:
      # Keep local files as is.
      local_file = cast(Iterable[pth.LocalPath], file)
      local_json = cast(Iterable[pth.LocalPath], json)
      local_csv = cast(Iterable[pth.LocalPath], csv)

    super().__init__(url, local_file, local_json, local_csv)

  @property
  def is_remote(self) -> bool:
    return self._is_remote

  def _copy_files(
      self, result_origin: ResultOrigin,
      paths: Optional[Iterable[pth.RemotePath]]
  ) -> Optional[Iterable[pth.LocalPath]]:
    if not paths:
      return []
    # Copy result files from remote tmp dir to local results dir
    browser_platform = result_origin.browser_platform
    remote_tmp_dir = result_origin.browser_tmp_dir
    out_dir = result_origin.out_dir
    local_result_paths: List[pth.LocalPath] = []
    for remote_path in paths:
      try:
        relative_path = remote_path.relative_to(remote_tmp_dir)
      except ValueError:
        logging.debug(
            "Browser result is not in browser tmp dir: "
            "only using the name of '%s'", remote_path)
        relative_path = result_origin.runner_platform.local_path(
            remote_path.name)
      local_result_path = out_dir / relative_path
      browser_platform.rsync(remote_path, local_result_path)
      assert local_result_path.exists(), "Failed to copy result file."
      local_result_paths.append(local_result_path)
    return local_result_paths


class ProbeResultDict:
  """
  Maps Probes to their result files Paths.
  """

  def __init__(self, path: pth.RemotePath) -> None:
    self._path = path
    self._dict: Dict[str, ProbeResult] = {}

  def __setitem__(self, probe: Probe, result: ProbeResult) -> None:
    assert isinstance(result, ProbeResult)
    self._dict[probe.name] = result

  def __getitem__(self, probe: Probe) -> ProbeResult:
    name = probe.name
    if name not in self._dict:
      raise KeyError(f"No results for probe='{name}'")
    return self._dict[name]

  def __contains__(self, probe: Probe) -> bool:
    return probe.name in self._dict

  def __bool__(self) -> bool:
    return bool(self._dict)

  def __len__(self) -> int:
    return len(self._dict)

  def get(self, probe: Probe, default: Any = None) -> ProbeResult:
    return self._dict.get(probe.name, default)

  def get_by_name(self, name: str, default: Any = None) -> ProbeResult:
    # Debug helper only.
    # Use bracket `results[probe]` or `results.get(probe)` instead.
    return self._dict.get(name, default)

  def to_json(self) -> JsonDict:
    data: JsonDict = {}
    for probe_name, results in self._dict.items():
      if isinstance(results, (pth.RemotePath, str)):
        data[probe_name] = str(results)
      else:
        if results.is_empty:
          if not probe_name.startswith(INTERNAL_NAME_PREFIX):
            logging.debug("probe=%s did not produce any data.", probe_name)
          data[probe_name] = None
        else:
          data[probe_name] = results.to_json()
    return data
