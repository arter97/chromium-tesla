# Copyright 2022 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Definitions of builders in the chromium.fuchsia builder group."""

load("//lib/branches.star", "branches")
load("//lib/builder_config.star", "builder_config")
load("//lib/builder_health_indicators.star", "health_spec")
load("//lib/builders.star", "free_space", "gardener_rotations", "os", "siso")
load("//lib/ci.star", "ci")
load("//lib/consoles.star", "consoles")
load("//lib/gn_args.star", "gn_args")

ci.defaults.set(
    executable = ci.DEFAULT_EXECUTABLE,
    builder_group = "chromium.fuchsia",
    builder_config_settings = builder_config.ci_settings(
        retry_failed_shards = True,
    ),
    pool = ci.DEFAULT_POOL,
    cores = 8,
    os = os.LINUX_DEFAULT,
    tree_closing = True,
    main_console_view = "main",
    cq_mirrors_console_view = "mirrors",
    execution_timeout = ci.DEFAULT_EXECUTION_TIMEOUT,
    gardener_rotations = gardener_rotations.CHROMIUM,
    health_spec = health_spec.DEFAULT,
    notifies = ["cr-fuchsia"],
    service_account = ci.DEFAULT_SERVICE_ACCOUNT,
    shadow_service_account = ci.DEFAULT_SHADOW_SERVICE_ACCOUNT,
    siso_enabled = True,
    siso_project = siso.project.DEFAULT_TRUSTED,
    siso_remote_jobs = siso.remote_jobs.HIGH_JOBS_FOR_CI,
)

consoles.console_view(
    name = "chromium.fuchsia",
    branch_selector = branches.selector.FUCHSIA_BRANCHES,
    ordering = {
        None: ["release", "debug"],
    },
)

ci.builder(
    name = "Deterministic Fuchsia (dbg)",
    executable = "recipe:swarming/deterministic_build",
    gn_args = gn_args.config(
        configs = [
            "debug_builder",
            "remoteexec",
            "fuchsia_smart_display",
        ],
    ),
    # Runs two builds, which can cause the builder to run out of disk space
    # with standard free space.
    free_space = free_space.high,
    console_view_entry = [
        consoles.console_view_entry(
            category = "det",
            short_name = "x64",
        ),
        consoles.console_view_entry(
            branch_selector = branches.selector.MAIN,
            console_view = "sheriff.fuchsia",
            category = "gardener|ci|x64",
            short_name = "det",
        ),
    ],
    contact_team_email = "chrome-fuchsia-engprod@google.com",
    execution_timeout = 6 * time.hour,
)

ci.builder(
    name = "fuchsia-arm64-cast-receiver-rel",
    branch_selector = branches.selector.FUCHSIA_BRANCHES,
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "fuchsia_arm64",
                "fuchsia_arm64_host",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_arch = builder_config.target_arch.ARM,
            target_bits = 64,
            target_platform = builder_config.target_platform.FUCHSIA,
        ),
        build_gs_bucket = "chromium-linux-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "fuchsia",
            "arm64_host",
            "cast_receiver_size_optimized",
        ],
    ),
    console_view_entry = [
        consoles.console_view_entry(
            category = "cast-receiver",
            short_name = "arm64",
        ),
        consoles.console_view_entry(
            branch_selector = branches.selector.MAIN,
            console_view = "sheriff.fuchsia",
            category = "gardener|ci|arm64",
            short_name = "cast",
        ),
    ],
    contact_team_email = "chrome-fuchsia-engprod@google.com",
)

ci.builder(
    name = "fuchsia-x64-cast-receiver-dbg",
    branch_selector = branches.selector.FUCHSIA_BRANCHES,
    description_html = "x64 debug build of fuchsia components with cast receiver",
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "fuchsia_x64",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.DEBUG,
            target_bits = 64,
            target_platform = builder_config.target_platform.FUCHSIA,
        ),
        build_gs_bucket = "chromium-linux-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "debug_builder",
            "remoteexec",
            "fuchsia",
            "cast_receiver_size_optimized",
        ],
    ),
    free_space = free_space.high,
    console_view_entry = [
        consoles.console_view_entry(
            category = "cast-receiver",
            short_name = "x64-dbg",
        ),
        consoles.console_view_entry(
            branch_selector = branches.selector.MAIN,
            console_view = "sheriff.fuchsia",
            category = "gardener|ci|x64",
            short_name = "cast-dbg",
        ),
    ],
    contact_team_email = "chrome-fuchsia-engprod@google.com",
)

ci.builder(
    name = "fuchsia-x64-cast-receiver-rel",
    branch_selector = branches.selector.FUCHSIA_BRANCHES,
    builder_spec = builder_config.builder_spec(
        gclient_config = builder_config.gclient_config(
            config = "chromium",
            apply_configs = [
                "fuchsia_x64",
            ],
        ),
        chromium_config = builder_config.chromium_config(
            config = "chromium",
            apply_configs = [
                "mb",
            ],
            build_config = builder_config.build_config.RELEASE,
            target_bits = 64,
            target_platform = builder_config.target_platform.FUCHSIA,
        ),
        build_gs_bucket = "chromium-linux-archive",
    ),
    gn_args = gn_args.config(
        configs = [
            "release_builder",
            "remoteexec",
            "fuchsia",
            "cast_receiver_size_optimized",
        ],
    ),
    console_view_entry = [
        consoles.console_view_entry(
            category = "cast-receiver",
            short_name = "x64",
        ),
        consoles.console_view_entry(
            branch_selector = branches.selector.MAIN,
            console_view = "sheriff.fuchsia",
            category = "gardener|ci|x64",
            short_name = "cast",
        ),
    ],
    contact_team_email = "chrome-fuchsia-engprod@google.com",
)
