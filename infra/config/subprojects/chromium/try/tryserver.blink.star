# Copyright 2021 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Definitions of builders in the tryserver.blink builder group."""

load("//lib/builders.star", "cpu", "os", "siso")
load("//lib/builder_config.star", "builder_config")
load("//lib/branches.star", "branches")
load("//lib/try.star", "try_")
load("//lib/consoles.star", "consoles")
load("//lib/gn_args.star", "gn_args")

try_.defaults.set(
    executable = try_.DEFAULT_EXECUTABLE,
    builder_group = "tryserver.blink",
    pool = try_.DEFAULT_POOL,
    cores = 8,
    execution_timeout = try_.DEFAULT_EXECUTION_TIMEOUT,
    service_account = try_.DEFAULT_SERVICE_ACCOUNT,
    siso_enabled = True,
    siso_project = siso.project.DEFAULT_UNTRUSTED,
    siso_remote_jobs = siso.remote_jobs.LOW_JOBS_FOR_CQ,
)

consoles.list_view(
    name = "tryserver.blink",
    branch_selector = branches.selector.DESKTOP_BRANCHES,
)

def blink_mac_builder(*, name, **kwargs):
    kwargs.setdefault("branch_selector", branches.selector.MAC_BRANCHES)
    kwargs.setdefault("builderless", True)
    kwargs.setdefault("cores", None)
    kwargs.setdefault("os", os.MAC_DEFAULT)
    kwargs.setdefault("ssd", True)
    return try_.builder(
        name = name,
        **kwargs
    )

try_.builder(
    name = "linux-blink-rel",
    branch_selector = branches.selector.LINUX_BRANCHES,
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.LINUX,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = False,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "minimal_symbols",
        ],
    ),
    os = os.LINUX_DEFAULT,
    main_list_view = "try",
)

# `linux-wpt-chromium-rel` (tests chrome) is distinct from `linux-blink-rel`
# (tests content shell) to avoid coupling their build configurations.
try_.builder(
    name = "linux-wpt-chromium-rel",
    description_html = """\
Runs <a href="https://web-platform-tests.org">web platform tests</a> against
Chrome.\
""",
    mirrors = ["ci/linux-wpt-chromium-rel"],
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = False,
    ),
    gn_args = "ci/linux-wpt-chromium-rel",
    os = os.LINUX_DEFAULT,
    contact_team_email = "chrome-blink-engprod@google.com",
    main_list_view = "try",
)

try_.builder(
    name = "win10-wpt-chromium-rel",
    description_html = """\
Runs <a href="https://web-platform-tests.org">web platform tests</a> against
Chrome.\
""",
    mirrors = ["ci/win10-wpt-chromium-rel"],
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = False,
    ),
    gn_args = "ci/win10-wpt-chromium-rel",
    builderless = True,
    os = os.WINDOWS_10,
    contact_team_email = "chrome-blink-engprod@google.com",
    main_list_view = "try",
)

try_.builder(
    name = "win10.20h2-blink-rel",
    branch_selector = branches.selector.WINDOWS_BRANCHES,
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.WIN,
        ),
        build_gs_bucket = "chromium-fyi-archive",
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = False,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "x64",
            "minimal_symbols",
        ],
    ),
    builderless = True,
    os = os.WINDOWS_ANY,
)

try_.builder(
    name = "win11-arm64-blink-rel",
    branch_selector = branches.selector.WINDOWS_BRANCHES,
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.WIN,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "arm64",
            "minimal_symbols",
        ],
    ),
    builderless = True,
    os = os.WINDOWS_ANY,
)

try_.builder(
    name = "win11-blink-rel",
    branch_selector = branches.selector.WINDOWS_BRANCHES,
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.WIN,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "x64",
            "minimal_symbols",
        ],
    ),
    builderless = True,
    os = os.WINDOWS_ANY,
)

blink_mac_builder(
    name = "mac10.15-blink-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "minimal_symbols",
            "x64",
        ],
    ),
    cores = None,
    cpu = cpu.ARM64,
)

blink_mac_builder(
    name = "mac11.0-blink-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "minimal_symbols",
        ],
    ),
    builderless = False,
)

blink_mac_builder(
    name = "mac11.0.arm64-blink-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
        build_gs_bucket = "chromium-fyi-archive",
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "arm64",
            "minimal_symbols",
        ],
    ),
    cores = None,
    cpu = cpu.ARM64,
)

blink_mac_builder(
    name = "mac12.0-blink-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = False,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "minimal_symbols",
            "x64",
        ],
    ),
    cpu = cpu.ARM64,
)

blink_mac_builder(
    name = "mac12.0.arm64-blink-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "arm64",
            "minimal_symbols",
        ],
    ),
    cores = None,
    cpu = cpu.ARM64,
)

blink_mac_builder(
    name = "mac13-blink-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = False,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "minimal_symbols",
            "x64",
        ],
    ),
    cores = None,
    cpu = cpu.ARM64,
)

try_.builder(
    name = "mac13-wpt-chromium-rel",
    description_html = """\
Runs <a href="https://web-platform-tests.org">web platform tests</a> against
Chrome.\
""",
    mirrors = ["ci/mac13-wpt-chromium-rel"],
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = False,
    ),
    gn_args = "ci/mac13-wpt-chromium-rel",
    builderless = True,
    cores = None,
    os = os.MAC_ANY,
    cpu = cpu.ARM64,
    contact_team_email = "chrome-blink-engprod@google.com",
    main_list_view = "try",
)

blink_mac_builder(
    name = "mac13.arm64-blink-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "arm64",
            "minimal_symbols",
        ],
    ),
    cores = None,
    cpu = cpu.ARM64,
)

blink_mac_builder(
    name = "mac13.arm64-skia-alt-blink-rel",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "arm64",
            "minimal_symbols",
        ],
    ),
    cores = None,
    cpu = cpu.ARM64,
)

blink_mac_builder(
    name = "mac14-blink-rel",
    description_html = """\
    Runs web tests against content-shell on Mac 14 (Intel).\
    """,
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = False,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "minimal_symbols",
            "x64",
        ],
    ),
    cpu = cpu.ARM64,
    contact_team_email = "chrome-blink-engprod@google.com",
)

blink_mac_builder(
    name = "mac14.arm64-blink-rel",
    description_html = """\
    Runs web tests against content-shell on Mac 14 (ARM).\
    """,
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.MAC,
        ),
    ),
    builder_config_settings = builder_config.try_settings(
        retry_failed_shards = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "chrome_with_codecs",
            "arm64",
            "minimal_symbols",
        ],
    ),
    cpu = cpu.ARM64,
    contact_team_email = "chrome-blink-engprod@google.com",
)
