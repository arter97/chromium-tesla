# Copyright 2021 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Definitions of builders in the tryserver.chromium.chromiumos builder group."""

load("//lib/branches.star", "branches")
load("//lib/builder_config.star", "builder_config")
load("//lib/builder_url.star", "linkify_builder")
load("//lib/builders.star", "os", "siso")
load("//lib/try.star", "try_")
load("//lib/consoles.star", "consoles")
load("//lib/gn_args.star", "gn_args")
load("//project.star", "settings")

try_.defaults.set(
    executable = try_.DEFAULT_EXECUTABLE,
    builder_group = "tryserver.chromium.chromiumos",
    pool = try_.DEFAULT_POOL,
    cores = 8,
    os = os.LINUX_DEFAULT,
    compilator_cores = 16,
    execution_timeout = try_.DEFAULT_EXECUTION_TIMEOUT,
    orchestrator_cores = 2,
    orchestrator_siso_remote_jobs = siso.remote_jobs.HIGH_JOBS_FOR_CQ,
    service_account = try_.DEFAULT_SERVICE_ACCOUNT,
    siso_enabled = True,
    siso_project = siso.project.DEFAULT_UNTRUSTED,
    siso_remote_jobs = siso.remote_jobs.LOW_JOBS_FOR_CQ,
)

consoles.list_view(
    name = "tryserver.chromium.chromiumos",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
)

try_.builder(
    name = "chromeos-amd64-generic-asan-rel",
    mirrors = [
        "ci/chromeos-amd64-generic-asan-rel",
    ],
    gn_args = "ci/chromeos-amd64-generic-asan-rel",
)

try_.builder(
    name = "chromeos-amd64-generic-cfi-thin-lto-rel",
    mirrors = [
        "ci/chromeos-amd64-generic-cfi-thin-lto-rel",
    ],
    # TODO(crbug.com/40605913): Enable DCHECKS on the two amd64-generic bots
    # when the PFQ has it enabled.
    gn_args = "ci/chromeos-amd64-generic-cfi-thin-lto-rel",
    # TODO(b/326865026): This build seems to have a high number of fallbacks,
    # but not enough to trigger the early fail mechanism.  The fallbacks result
    # in slow builds and timeouts.  Fail in these cases so logs are collected
    # for debugging.
    reclient_bootstrap_env = {
        "RBE_fail_early_min_fallback_ratio": "0.1",
    },
)

try_.builder(
    name = "chromeos-amd64-generic-dbg",
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/chromeos-amd64-generic-dbg",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/chromeos-amd64-generic-dbg",
        ],
    ),
    main_list_view = "try",
    tryjob = try_.job(
        location_filters = [
            "content/gpu/.+",
            "media/.+",
        ],
    ),
)

try_.builder(
    name = "chromeos-amd64-generic-rel",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    description_html = "This is a compile only builder for Ash chrome." +
                       " This builder also build Lacros with alternative toolchain.",
    mirrors = ["ci/chromeos-amd64-generic-rel"],
    gn_args = gn_args.config(
        configs = [
            "ci/chromeos-amd64-generic-rel",
            "dcheck_always_on",
        ],
    ),
    contact_team_email = "chromeos-sw-engprod@google.com",
    main_list_view = "try",
)

try_.orchestrator_builder(
    name = "chromeos-amd64-generic-rel-gtest",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    description_html = "This is an Ash chrome builder which only runs gtest." +
                       " This builder is the default CQ builder for" +
                       " non-ChromeOS engineers only. See the builder" +
                       " description for " +
                       linkify_builder("try", "chromeos-amd64-generic-rel-gtest-and-tast", "chromium") +
                       " for more information",
    mirrors = [
        "ci/chromeos-amd64-generic-rel",
        "ci/chromeos-amd64-generic-rel-gtest",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/chromeos-amd64-generic-rel",
            "dcheck_always_on",
        ],
    ),
    compilator = "chromeos-amd64-generic-rel-gtest-compilator",
    contact_team_email = "chromeos-sw-engprod@google.com",
    main_list_view = "try",
    tryjob = try_.job(
        equivalent_builder = "try/chromeos-amd64-generic-rel-gtest-and-tast",
        equivalent_builder_percentage = 100,
        equivalent_builder_whitelist = "google/chromeos-pa@google.com",
    ),
)

try_.orchestrator_builder(
    name = "chromeos-amd64-generic-rel-gtest-and-tast",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    description_html = "This is an Ash chrome builder which runs gtest" +
                       " and Tast tests. This builder is the default CQ" +
                       " builder for ChromeOS engineers only." +
                       " For a CL, infra would check the CL’s owner to see" +
                       " if the owner is a ChromeOS org engineer or not." +
                       " If the owner is a ChromeOS org engineer, the" +
                       " default CQ would include this builder which runs" +
                       " both Tast tests and gtests. Otherwise, the default" +
                       " CQ would include `chromeos-amd64-generic-rel-gtest`" +
                       " which only runs gtests. If you encounter unexpected" +
                       " Tast tests failures, please contact ChromeOS" +
                       " gardeners for help.",
    mirrors = [
        "ci/chromeos-amd64-generic-rel",
        "ci/chromeos-amd64-generic-rel-gtest",
        "ci/chromeos-amd64-generic-rel-tast",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/chromeos-amd64-generic-rel",
            "dcheck_always_on",
        ],
    ),
    compilator = "chromeos-amd64-generic-rel-gtest-and-tast-compilator",
    contact_team_email = "chromeos-sw-engprod@google.com",
    main_list_view = "try",
    tryjob = try_.job(
        omit_from_luci_cv = True,
    ),
)

CHROMEOS_SHARED_CACHE = "shared_chromeos_amd64_generic_rel_cache"

try_.compilator_builder(
    name = "chromeos-amd64-generic-rel-gtest-compilator",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    description_html = ".",
    cores = "16",
    caches = [
        swarming.cache(
            name = CHROMEOS_SHARED_CACHE,
            path = "builder",
            wait_for_warm_cache = 4 * time.minute,
        ),
    ],
    contact_team_email = "chromeos-sw-engprod@google.com",
    main_list_view = "try",
)

try_.compilator_builder(
    name = "chromeos-amd64-generic-rel-gtest-and-tast-compilator",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    description_html = ".",
    cores = "16",
    caches = [
        swarming.cache(
            name = CHROMEOS_SHARED_CACHE,
            path = "builder",
            wait_for_warm_cache = 4 * time.minute,
        ),
    ],
    contact_team_email = "chromeos-sw-engprod@google.com",
    main_list_view = "try",
)

try_.builder(
    name = "chromeos-arm-generic-dbg",
    mirrors = [
        "ci/chromeos-arm-generic-dbg",
    ],
    gn_args = "ci/chromeos-arm-generic-dbg",
)

# crbug.com/40207910
try_.builder(
    name = "linux-chromeos-dbg-oslogin",
    description_html = "Try job to test changes that may fix OSLogin issues with tests on ChromeOS.",
    mirrors = [
        "ci/linux-chromeos-dbg-oslogin",
    ],
    gn_args = "ci/linux-chromeos-dbg-oslogin",
    contact_team_email = "chrome-dev-infra-team@google.com",
)

try_.builder(
    name = "chromeos-arm-generic-rel",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    mirrors = ["ci/chromeos-arm-generic-rel"],
    gn_args = gn_args.config(
        configs = [
            "ci/chromeos-arm-generic-rel",
            "dcheck_always_on",
        ],
    ),
    builderless = not settings.is_main,
    experiments = {
        # crbug/940930
        "chromium.enable_cleandead": 100,
    },
    main_list_view = "try",
    tryjob = try_.job(),
)

try_.builder(
    name = "chromeos-arm64-generic-rel",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    mirrors = ["ci/chromeos-arm64-generic-rel"],
    gn_args = "ci/chromeos-arm64-generic-rel",
)

try_.builder(
    name = "chromeos-libfuzzer-asan-rel",
    # TODO(crbug.com/41492669): Can delete this description when it's
    # automatically generated.
    description_html = "Trybot of {}.".format(linkify_builder("ci", "Libfuzzer Upload Chrome OS ASan")),
    executable = "recipe:chromium/fuzz",
    mirrors = ["ci/Libfuzzer Upload Chrome OS ASan"],
    gn_args = gn_args.config(
        configs = [
            "ci/Libfuzzer Upload Chrome OS ASan",
            "dcheck_always_on",
            "no_symbols",
            "skip_generate_fuzzer_owners",
        ],
    ),
    contact_team_email = "chrome-deet-core@google.com",
)

try_.orchestrator_builder(
    name = "lacros-amd64-generic-rel-gtest",
    branch_selector = branches.selector.CROS_BRANCHES,
    description_html = "This is a Lacros chrome builder which only runs gtest." +
                       " This builder is the default CQ builder for" +
                       " non-ChromeOS engineers only. See the builder" +
                       " description for " +
                       linkify_builder("try", "lacros-amd64-generic-rel-gtest-and-tast", "chromium") +
                       " for more information",
    mirrors = [
        "ci/lacros-amd64-generic-rel",
        "ci/lacros-amd64-generic-rel-gtest",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/lacros-amd64-generic-rel",
            "dcheck_always_on",
        ],
    ),
    compilator = "lacros-amd64-generic-rel-gtest-compilator",
    contact_team_email = "chrome-desktop-engprod@google.com",
    main_list_view = "try",
    tryjob = try_.job(
        equivalent_builder = "try/lacros-amd64-generic-rel-gtest-and-tast",
        equivalent_builder_percentage = 100,
        equivalent_builder_whitelist = "google/chromeos-pa@google.com",
    ),
)

try_.orchestrator_builder(
    name = "lacros-amd64-generic-rel-gtest-and-tast",
    branch_selector = branches.selector.CROS_BRANCHES,
    description_html = "This is a Lacros chrome builder which runs gtest" +
                       " and Tast tests. This builder is the default CQ" +
                       " builder for ChromeOS engineers only." +
                       " For a CL, infra would check the CL’s owner to see" +
                       " if the owner is a ChromeOS org engineer or not." +
                       " If the owner is a ChromeOS org engineer, the" +
                       " default CQ would include this builder which runs" +
                       " both Tast tests and gtests. Otherwise, the default" +
                       " CQ would include `lacros-amd64-generic-rel-gtest`" +
                       " which only runs gtests. If you encounter unexpected" +
                       " Tast tests failures, please contact ChromeOS" +
                       " gardeners for help.",
    mirrors = [
        "ci/lacros-amd64-generic-rel",
        "ci/lacros-amd64-generic-rel-gtest",
        "ci/lacros-amd64-generic-rel-tast",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/lacros-amd64-generic-rel",
            "dcheck_always_on",
        ],
    ),
    compilator = "lacros-amd64-generic-rel-gtest-and-tast-compilator",
    contact_team_email = "chrome-desktop-engprod@google.com",
    main_list_view = "try",
    tryjob = try_.job(
        omit_from_luci_cv = True,
    ),
)

LACROS_SHARED_CACHE = "shared_lacros_amd64_generic_rel_cache"

try_.compilator_builder(
    name = "lacros-amd64-generic-rel-gtest-compilator",
    branch_selector = branches.selector.CROS_BRANCHES,
    description_html = ".",
    cores = "8",
    caches = [
        swarming.cache(
            name = LACROS_SHARED_CACHE,
            path = "builder",
            wait_for_warm_cache = 4 * time.minute,
        ),
    ],
    contact_team_email = "chrome-desktop-engprod@google.com",
    main_list_view = "try",
)

try_.compilator_builder(
    name = "lacros-amd64-generic-rel-gtest-and-tast-compilator",
    branch_selector = branches.selector.CROS_BRANCHES,
    description_html = ".",
    cores = "8",
    caches = [
        swarming.cache(
            name = LACROS_SHARED_CACHE,
            path = "builder",
            wait_for_warm_cache = 4 * time.minute,
        ),
    ],
    contact_team_email = "chrome-desktop-engprod@google.com",
    main_list_view = "try",
)

try_.builder(
    name = "lacros-amd64-generic-rel-non-skylab",
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/lacros-amd64-generic-rel-non-skylab",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/lacros-amd64-generic-rel-non-skylab",
            "dcheck_always_on",
        ],
    ),
    contact_team_email = "chrome-desktop-engprod@google.com",
    main_list_view = "try",
)

try_.builder(
    name = "chromeos-amd64-generic-lacros-dbg",
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/chromeos-amd64-generic-lacros-dbg",
    ],
    gn_args = "ci/chromeos-amd64-generic-lacros-dbg",
)

try_.builder(
    name = "lacros-arm-generic-rel",
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/lacros-arm-generic-rel",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/lacros-arm-generic-rel",
            "dcheck_always_on",
        ],
    ),
    builderless = not settings.is_main,
    experiments = {
        # crbug/940930
        "chromium.enable_cleandead": 100,
    },
    main_list_view = "try",
    tryjob = try_.job(),
)

try_.builder(
    name = "lacros-arm-generic-rel-skylab",
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/lacros-arm-generic-rel-skylab",
    ],
    gn_args = "ci/lacros-arm-generic-rel-skylab",
    contact_team_email = "chrome-desktop-engprod@google.com",
    main_list_view = "try",
)

try_.builder(
    name = "lacros-arm64-generic-rel",
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/lacros-arm64-generic-rel",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/lacros-arm64-generic-rel",
            "dcheck_always_on",
        ],
    ),
    main_list_view = "try",
)

try_.builder(
    name = "lacros-arm64-generic-rel-skylab",
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/lacros-arm64-generic-rel-skylab",
    ],
    gn_args = "ci/lacros-arm64-generic-rel-skylab",
    contact_team_email = "chrome-desktop-engprod@google.com",
    main_list_view = "try",
)

try_.builder(
    name = "linux-chromeos-compile-dbg",
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/linux-chromeos-dbg",
    ],
    builder_config_settings = builder_config.try_settings(
        include_all_triggered_testers = True,
        is_compile_only = True,
    ),
    gn_args = gn_args.config(
        configs = [
            "ci/linux-chromeos-dbg",
            "no_symbols",
        ],
    ),
    builderless = not settings.is_main,
    experiments = {
        # crbug/940930
        "chromium.enable_cleandead": 100,
    },
    main_list_view = "try",
    siso_enabled = True,
    siso_remote_jobs = siso.remote_jobs.HIGH_JOBS_FOR_CQ,
    tryjob = try_.job(),
)

try_.builder(
    name = "chromeos-jacuzzi-rel",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    description_html = """
This builder builds chromium and tests it on the public CrOS image on skylab DUTs.
""",
    mirrors = [
        "ci/chromeos-jacuzzi-rel",
    ],
    gn_args = "ci/chromeos-jacuzzi-rel",
    contact_team_email = "chromeos-velocity@google.com",
    execution_timeout = 8 * time.hour,
    main_list_view = "try",
)

try_.builder(
    name = "chromeos-octopus-rel",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    description_html = """
This builder builds chromium and tests it on the public CrOS image on skylab DUTs.
""",
    mirrors = [
        "ci/chromeos-octopus-rel",
    ],
    gn_args = "ci/chromeos-octopus-rel",
    contact_team_email = "chromeos-velocity@google.com",
    execution_timeout = 8 * time.hour,
    main_list_view = "try",
)

try_.orchestrator_builder(
    name = "linux-chromeos-rel",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    mirrors = [
        "ci/linux-chromeos-rel",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/linux-chromeos-rel",
            "release_try_builder",
            "no_symbols",
            "use_clang_coverage",
            "partial_code_coverage_instrumentation",
            "enable_dangling_raw_ptr_feature_flag",
            "enable_backup_ref_ptr_feature_flag",
        ],
    ),
    compilator = "linux-chromeos-rel-compilator",
    coverage_test_types = ["unit", "overall"],
    experiments = {
        # go/nplus1shardsproposal
        "chromium.add_one_test_shard": 10,
        # crbug/940930
        "chromium.enable_cleandead": 100,
    },
    main_list_view = "try",
    # TODO(crbug.com/40241638): Use orchestrator pool once overloaded test pools
    # are addressed
    # use_orchestrator_pool = True,
    tryjob = try_.job(),
    use_clang_coverage = True,
)

try_.compilator_builder(
    name = "linux-chromeos-rel-compilator",
    branch_selector = branches.selector.CROS_LTS_BRANCHES,
    cores = 32,
    main_list_view = "try",
)

try_.builder(
    name = "linux-lacros-dbg",
    branch_selector = branches.selector.CROS_BRANCHES,
    # TODO(crbug.com/40780788) Adds the CI tester when it's available.
    mirrors = [
        "ci/linux-lacros-dbg",
    ],
    gn_args = "ci/linux-lacros-dbg",
)

try_.orchestrator_builder(
    name = "linux-lacros-rel",
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/linux-lacros-builder-rel",
        "ci/linux-lacros-tester-rel",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/linux-lacros-builder-rel",
            "release_try_builder",
            "clang",
            "use_clang_coverage",
            "partial_code_coverage_instrumentation",
        ],
    ),
    compilator = "linux-lacros-rel-compilator",
    coverage_test_types = ["unit", "overall"],
    experiments = {
        # go/nplus1shardsproposal
        "chromium.add_one_test_shard": 10,
        # crbug/940930
        "chromium.enable_cleandead": 50,
    },
    main_list_view = "try",
    # TODO(crbug.com/40241638): Use orchestrator pool once overloaded test pools
    # are addressed
    # use_orchestrator_pool = True,
    tryjob = try_.job(),
    use_clang_coverage = True,
)

try_.compilator_builder(
    name = "linux-lacros-rel-compilator",
    branch_selector = branches.selector.CROS_BRANCHES,
    cores = 32,
    main_list_view = "try",
)

try_.builder(
    name = "linux-chromeos-dbg",
    # The CI builder that this mirrors is enabled on branches, so this will
    # allow testing changes that would break it before submitting
    branch_selector = branches.selector.CROS_BRANCHES,
    mirrors = [
        "ci/linux-chromeos-dbg",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/linux-chromeos-dbg",
        ],
    ),
)

try_.builder(
    name = "linux-chromeos-annotator-rel",
    mirrors = [
        "ci/linux-chromeos-annotator-rel",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/linux-chromeos-annotator-rel",
            "try_builder",
            "no_symbols",
            "also_build_lacros_chrome",
            "use_clang_coverage",
            "partial_code_coverage_instrumentation",
            "enable_dangling_raw_ptr_feature_flag",
            "enable_backup_ref_ptr_feature_flag",
        ],
    ),
)

try_.builder(
    name = "linux-cfm-rel",
    mirrors = [
        "ci/linux-cfm-rel",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/linux-cfm-rel",
            "release_try_builder",
        ],
    ),
    siso_project = siso.project.DEFAULT_UNTRUSTED,
    tryjob = try_.job(
        location_filters = [
            "chromeos/ash/components/chromebox_for_meetings/.+",
            "chromeos/ash/components/dbus/chromebox_for_meetings/.+",
            "chromeos/services/chromebox_for_meetings/.+",
            "chrome/browser/ash/chromebox_for_meetings/.+",
        ],
    ),
)

try_.builder(
    name = "linux-lacros-rel-dangling-ptr-fyi",
    description_html = "Dangling ptr check for lacros.",
    mirrors = [
        "ci/linux-lacros-rel-dangling-ptr-fyi",
    ],
    gn_args = gn_args.config(
        configs = [
            "ci/linux-lacros-rel-dangling-ptr-fyi",
        ],
    ),
    contact_team_email = "chrome-desktop-engprod@google.com",
    execution_timeout = 8 * time.hour,
)
