# Copyright 2023 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This file contains suite definitions that can be used in
# //testing/buildbot/waterfalls.pyl and will also be usable for builders that
# set their tests in starlark (once that is ready). The legacy_ prefix on the
# declarations indicates the capability to be used in //testing/buildbot. Once a
# suite is no longer needed in //testing/buildbot, targets.bundle (which does
# not yet exist) can be used for grouping tests in a more flexible manner.

load("//lib/targets.star", "targets")

targets.legacy_matrix_compound_suite(
    name = "android_11_emulator_gtests",
    basic_suites = {
        "android_emulator_specific_chrome_public_tests": None,
        "android_trichrome_smoke_tests": None,
        "android_smoke_tests": None,
        "android_specific_chromium_gtests": None,  # Already includes gl_gtests.
        "chromium_gtests": None,
        "chromium_gtests_for_devices_with_graphical_output": None,
        "linux_flavor_specific_chromium_gtests": None,
        "system_webview_shell_instrumentation_tests": None,  # Not an experimental test
        "webview_trichrome_cts_tests_suite": targets.legacy_matrix_config(
            variants = [
                "WEBVIEW_TRICHROME_FULL_CTS_TESTS",
                "WEBVIEW_TRICHROME_INSTANT_CTS_TESTS",
            ],
        ),
        "webview_ui_instrumentation_tests": None,
    },
)

targets.legacy_matrix_compound_suite(
    name = "android_12_emulator_gtests",
    basic_suites = {
        "android_ci_only_fieldtrial_webview_tests": None,
        "android_emulator_specific_chrome_public_tests": None,
        "android_trichrome_smoke_tests": None,
        "android_smoke_tests": None,
        "android_specific_chromium_gtests": None,  # Already includes gl_gtests.
        "chrome_profile_generator_tests": None,
        "chromium_gtests": None,
        "chromium_gtests_for_devices_with_graphical_output": None,
        "fieldtrial_android_tests": None,
        "jni_zero_sample_apk_test_suite": None,
        "linux_flavor_specific_chromium_gtests": None,
        "minidump_uploader_tests": None,
        "system_webview_shell_instrumentation_tests": None,  # Not an experimental test
        "webview_trichrome_64_cts_tests_suite": targets.legacy_matrix_config(
            variants = [
                "WEBVIEW_TRICHROME_FULL_CTS_TESTS",
                "WEBVIEW_TRICHROME_INSTANT_CTS_TESTS",
            ],
        ),
        "webview_ui_instrumentation_tests": None,
    },
)

targets.legacy_matrix_compound_suite(
    name = "android_12l_emulator_gtests",
    basic_suites = {
        "android_emulator_specific_chrome_public_tests": None,
        "android_trichrome_smoke_tests": None,
        "android_smoke_tests": None,
        "android_specific_chromium_gtests": None,  # Already includes gl_gtests.
        "chromium_gtests": None,
        "chromium_gtests_for_devices_with_graphical_output": None,
        "linux_flavor_specific_chromium_gtests": None,
        "system_webview_shell_instrumentation_tests": None,  # Not an experimental test
        "webview_ui_instrumentation_tests": None,
    },
)

targets.legacy_matrix_compound_suite(
    name = "android_13_emulator_gtests",
    basic_suites = {
        "android_ci_only_fieldtrial_webview_tests": None,
        "android_emulator_specific_chrome_public_tests": None,
        "android_trichrome_smoke_tests": None,
        "android_smoke_tests": None,
        "android_specific_chromium_gtests": None,  # Already includes gl_gtests.
        "chrome_profile_generator_tests": None,
        "chromium_gtests": None,
        "chromium_gtests_for_devices_with_graphical_output": None,
        "fieldtrial_android_tests": None,
        "jni_zero_sample_apk_test_suite": None,
        "linux_flavor_specific_chromium_gtests": None,
        "minidump_uploader_tests": None,
        "system_webview_shell_instrumentation_tests": None,  # Not an experimental test
        "webview_trichrome_64_cts_tests_suite": targets.legacy_matrix_config(
            variants = [
                "WEBVIEW_TRICHROME_FULL_CTS_TESTS",
                "WEBVIEW_TRICHROME_INSTANT_CTS_TESTS",
            ],
        ),
        "webview_ui_instrumentation_tests": None,
    },
)

targets.legacy_matrix_compound_suite(
    name = "android_14_emulator_gtests",
    basic_suites = {
        "android_emulator_specific_chrome_public_tests": None,
        "android_trichrome_smoke_tests": None,
        "android_smoke_tests": None,
        "android_specific_chromium_gtests": None,  # Already includes gl_gtests.
        "chromium_gtests": None,
        "chromium_gtests_for_devices_with_graphical_output": None,
        "linux_flavor_specific_chromium_gtests": None,
        "system_webview_shell_instrumentation_tests": None,  # Not an experimental test
        "webview_trichrome_64_cts_tests_suite": targets.legacy_matrix_config(
            variants = [
                "WEBVIEW_TRICHROME_FULL_CTS_TESTS",
                "WEBVIEW_TRICHROME_INSTANT_CTS_TESTS",
            ],
        ),
        "webview_trichrome_64_cts_tests_no_field_trial_suite": None,
        "webview_ui_instrumentation_tests": None,
    },
)

targets.legacy_matrix_compound_suite(
    name = "android_15_emulator_gtests",
    basic_suites = {
        "android_emulator_specific_chrome_public_tests": None,
        "android_trichrome_smoke_tests": None,
        "android_smoke_tests": None,
        "android_specific_chromium_gtests": None,  # Already includes gl_gtests.
        "chromium_gtests": None,
        "chromium_gtests_for_devices_with_graphical_output": None,
        "linux_flavor_specific_chromium_gtests": None,
        "system_webview_shell_instrumentation_tests": None,  # Not an experimental test
        "webview_trichrome_64_cts_tests_suite": targets.legacy_matrix_config(
            variants = [
                "WEBVIEW_TRICHROME_FULL_CTS_TESTS",
                "WEBVIEW_TRICHROME_INSTANT_CTS_TESTS",
            ],
        ),
        "webview_trichrome_64_cts_tests_no_field_trial_suite": None,
        "webview_ui_instrumentation_tests": None,
    },
)

targets.legacy_matrix_compound_suite(
    name = "android_fieldtrial_rel_webview_tests",
    basic_suites = {
        "fieldtrial_android_tests": None,
        "webview_bot_instrumentation_test_apk_gtest": targets.legacy_matrix_config(
            variants = [
                "DISABLE_FIELD_TRIAL_CONFIG",
                "SINGLE_GROUP_PER_STUDY_PREFER_EXISTING_BEHAVIOR",
                "SINGLE_GROUP_PER_STUDY_PREFER_NEW_BEHAVIOR",
            ],
        ),
        "webview_trichrome_64_cts_field_trial_tests": targets.legacy_matrix_config(
            variants = [
                "DISABLE_FIELD_TRIAL_CONFIG",
                "SINGLE_GROUP_PER_STUDY_PREFER_EXISTING_BEHAVIOR",
                "SINGLE_GROUP_PER_STUDY_PREFER_NEW_BEHAVIOR",
            ],
        ),
        "webview_ui_instrumentation_tests": targets.legacy_matrix_config(
            variants = [
                "DISABLE_FIELD_TRIAL_CONFIG",
                "SINGLE_GROUP_PER_STUDY_PREFER_EXISTING_BEHAVIOR",
                "SINGLE_GROUP_PER_STUDY_PREFER_NEW_BEHAVIOR",
            ],
        ),
        "system_webview_shell_instrumentation_tests": targets.legacy_matrix_config(
            variants = [
                "DISABLE_FIELD_TRIAL_CONFIG_WEBVIEW_COMMANDLINE",
                "SINGLE_GROUP_PER_STUDY_PREFER_EXISTING_BEHAVIOR_WEBVIEW_COMMANDLINE",
                "SINGLE_GROUP_PER_STUDY_PREFER_NEW_BEHAVIOR_WEBVIEW_COMMANDLINE",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_betty_vmlab_tests",
    basic_suites = {
        "chromeos_chrome_all_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_chrome_criticalstaging_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_chrome_disabled_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_system_friendly_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_vaapi_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_brya_skylab_tests",
    basic_suites = {
        "chromeos_chrome_all_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_chrome_criticalstaging_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_chrome_disabled_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_system_friendly_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_vaapi_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_jacuzzi_rel_skylab_tests",
    basic_suites = {
        # After the builder gets stabilized, 'chromeos_device_only_gtests' will
        # be tried to be replaced with 'chromeos_system_friendly_gtests'.
        "chromeos_device_only_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_PUBLIC_LKGM",
            ],
        ),
        "chromeos_chrome_all_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "chromeos-tast-public-builder",
                # jacuzzi is slow. So that we use more number of shards.
                "shards-30",
            ],
            variants = [
                "CROS_PUBLIC_LKGM",
            ],
        ),
        "chromeos_chrome_criticalstaging_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "chromeos-tast-public-builder",
            ],
            variants = [
                "CROS_PUBLIC_LKGM",
            ],
        ),
        "chromeos_chrome_disabled_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "chromeos-tast-public-builder",
            ],
            variants = [
                "CROS_PUBLIC_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_jacuzzi_skylab_tests",
    basic_suites = {
        "chromeos_chrome_all_tast_tests": targets.legacy_matrix_config(
            mixins = [
                # jacuzzi is slow. So that we use more number of shards.
                "shards-30",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_chrome_criticalstaging_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_chrome_disabled_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_device_only_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_octopus_rel_skylab_tests",
    basic_suites = {
        # After the builder gets stabilized, 'chromeos_device_only_gtests' will
        # be tried to be replaced with 'chromeos_system_friendly_gtests'.
        "chromeos_device_only_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_PUBLIC_LKGM",
            ],
        ),
        "chromeos_chrome_all_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "chromeos-tast-public-builder",
            ],
            variants = [
                "CROS_PUBLIC_LKGM",
            ],
        ),
        "chromeos_chrome_criticalstaging_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "chromeos-tast-public-builder",
            ],
            variants = [
                "CROS_PUBLIC_LKGM",
            ],
        ),
        "chromeos_chrome_disabled_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "chromeos-tast-public-builder",
            ],
            variants = [
                "CROS_PUBLIC_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_octopus_skylab_tests",
    basic_suites = {
        "chromeos_chrome_all_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_device_only_gtests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_trogdor_skylab_tests",
    basic_suites = {
        "chromeos_chrome_all_tast_tests": targets.legacy_matrix_config(
            mixins = [
                # trogdor is slow. So that we use more number of shards.
                "shards-20",
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_device_only_gtests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_volteer_skylab_tests",
    basic_suites = {
        "chromeos_chrome_all_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_chrome_criticalstaging_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_chrome_disabled_tast_tests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_device_only_gtests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_brya_preuprev_tests",
    basic_suites = {
        "chromeos_chrome_cq_medium_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_device_only_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_jacuzzi_preuprev_tests",
    basic_suites = {
        "chromeos_chrome_cq_medium_tast_tests": targets.legacy_matrix_config(
            mixins = [
                # jacuzzi is slow. So that we use more number of shards.
                "shards-10",
            ],
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_device_only_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "chromeos_volteer_preuprev_tests",
    basic_suites = {
        "chromeos_chrome_cq_medium_tast_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
        "chromeos_device_only_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "dawn_chromeos_release_tests_volteer_skylab",
    basic_suites = {
        # gtests
        "gpu_common_gtests_passthrough": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
        "gpu_dawn_gtests": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
        "gpu_dawn_gtests_with_validation": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "dawn_chromeos_release_telemetry_tests_volteer_skylab",
    basic_suites = {
        # TODO(crbug.com/340815322): Add gpu_dawn_webgpu_compat_cts once
        # compat works properly on ChromeOS.
        "gpu_dawn_webgpu_cts": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "fieldtrial_ios_simulator_tests",
    basic_suites = {
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "disable_field_trial_config_for_earl_grey",
            ],
            variants = [
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPHONE_13_16_4",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "disable_field_trial_config_for_earl_grey",
            ],
            variants = [
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPHONE_13_16_4",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "gpu_angle_ios_gtests",
    basic_suites = {
        "gpu_angle_ios_end2end_gtests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
            ],
        ),
        "gpu_angle_ios_white_box_gtests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "gpu_fyi_chromeos_release_gtests_volteer_skylab",
    basic_suites = {
        # gpu_angle_unit_gtests and gpu_desktop_specific_gtests should also be
        # enabled here, but are removed for various reasons. See the definition
        # for gpu_fyi_chromeos_release_gtests in compound_suites.star for more
        # information.
        "gpu_common_gtests_passthrough": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "gpu_fyi_chromeos_release_telemetry_tests_jacuzzi_skylab",
    basic_suites = {
        "gpu_common_and_optional_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_JACUZZI_RELEASE_LKGM",
            ],
        ),
        "gpu_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_JACUZZI_RELEASE_LKGM",
            ],
        ),
        "gpu_webcodecs_telemetry_test": targets.legacy_matrix_config(
            variants = [
                "CROS_JACUZZI_RELEASE_LKGM",
            ],
        ),
        "gpu_webgl_conformance_gles_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_JACUZZI_RELEASE_LKGM",
            ],
        ),
        "gpu_webgl2_conformance_gles_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_JACUZZI_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "gpu_fyi_chromeos_release_telemetry_tests_volteer_skylab",
    basic_suites = {
        "gpu_common_and_optional_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
        "gpu_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
        "gpu_webcodecs_telemetry_test": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
        "gpu_webgl_conformance_gles_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
        "gpu_webgl2_conformance_gles_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_RELEASE_ASH_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "gpu_fyi_lacros_device_release_telemetry_tests",
    basic_suites = {
        "gpu_pixel_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "LACROS_ASH_TOT",
            ],
        ),
        "gpu_webgl_conformance_gles_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "LACROS_ASH_TOT",
            ],
        ),
        "gpu_webgl2_conformance_gles_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "LACROS_ASH_TOT",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios17_beta_simulator_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_17_5",
                "SIM_IPHONE_SE_3RD_GEN_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_crash_xcuitests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios17_sdk_simulator_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPAD_AIR_5TH_GEN_17_5",
                "SIM_IPHONE_14_17_5",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_17_5",
                "SIM_IPHONE_SE_3RD_GEN_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios18_beta_simulator_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_10TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_10TH_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_PRO_MAX_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
            ],
        ),
        "ios_crash_xcuitests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios18_sdk_simulator_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
            ],
        ),
        "ios_crash_xcuitests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_SE_3RD_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios_asan_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_X_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_X_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios_blink_dbg_tests",
    basic_suites = {
        "ios_blink_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios_clang_tot_device_tests",
    basic_suites = {
        "clang_tot_gtests": targets.legacy_matrix_config(
            # TODO(b/337049057): Change to iOS16+ devices once ready.
            variants = [
                "IPHONE_7_15_4_1",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios_clang_tot_sim_tests",
    basic_suites = {
        "clang_tot_gtests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_X_16_4",
            ],
        ),
    },
)

# This suite is a union of ios_simulator_tests and
# ios_simulator_full_configs_tests.
targets.legacy_matrix_compound_suite(
    name = "ios_code_coverage_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_PLUS_16_4",
                "SIM_IPHONE_14_PLUS_17_5",
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios_m1_simulator_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_10TH_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_10TH_GEN_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_PRO_MAX_16_4",
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_SE_3RD_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPHONE_14_PRO_MAX_17_5",
                "SIM_IPHONE_14_17_5",
                "SIM_IPHONE_SE_3RD_GEN_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
    },
)

# Please also change ios_code_coverage_tests for any change in this suite.
targets.legacy_matrix_compound_suite(
    name = "ios_simulator_full_configs_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_PLUS_16_4",
                "SIM_IPHONE_14_PLUS_17_5",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios_simulator_multi_window_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios_simulator_noncq_tests",
    basic_suites = {
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
                "record_failed_tests",
            ],
            variants = [
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_PLUS_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPHONE_SE_3RD_GEN_16_4",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPHONE_14_PLUS_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
                "SIM_IPHONE_SE_3RD_GEN_17_5",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
            ],
        ),
        "ios_crash_xcuitests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_SE_3RD_GEN_16_4",
                "SIM_IPHONE_SE_3RD_GEN_17_5",
            ],
        ),
    },
)

# Please also change ios_code_coverage_tests for any change in this suite.
targets.legacy_matrix_compound_suite(
    name = "ios_simulator_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPHONE_14_17_5",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_PRO_6TH_GEN_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_PRO_6TH_GEN_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios_webkit_tot_tests",
    basic_suites = {
        "ios_common_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_eg2_cq_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_eg2_tests": targets.legacy_matrix_config(
            mixins = [
                "xcodebuild_sim_runner",
            ],
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
        "ios_screen_size_dependent_tests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "ios_webrtc_fyi_tests",
    basic_suites = {
        "ios_remoting_fyi_unittests": targets.legacy_matrix_config(
            variants = [
                "SIM_IPHONE_14_16_4",
                "SIM_IPAD_AIR_5TH_GEN_16_4",
                "SIM_IPHONE_14_17_5",
                "SIM_IPAD_AIR_5TH_GEN_17_5",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "lacros_arm64_generic_rel_skylab",
    basic_suites = {
        "lacros_skylab_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_TROGDOR_PUBLIC_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            variants = [
                "CROS_TROGDOR_PUBLIC_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "lacros_device_or_vm_tests",
    basic_suites = {
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            variants = [
                "LACROS_AMD64_GENERIC",
            ],
        ),
        "lacros_all_tast_tests_suite": targets.legacy_matrix_config(
            variants = [
                "LACROS_AMD64_GENERIC",
            ],
        ),
        "lacros_device_or_vm_gtests": targets.legacy_matrix_config(
            variants = [
                "LACROS_AMD64_GENERIC",
            ],
        ),
        "lacros_vm_gtests": targets.legacy_matrix_config(
            variants = [
                "LACROS_AMD64_GENERIC",
            ],
        ),
    },
)

# Check go/lacros-on-skylab for details of Skylab configurations.
targets.legacy_matrix_compound_suite(
    name = "lacros_skylab_arm64",
    basic_suites = {
        "lacros_skylab_tests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_STRONGBAD_RELEASE_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_STRONGBAD_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "lacros_skylab_tests_amd64_generic",
    basic_suites = {
        "lacros_skylab_tests_version_skew": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_BRYA_RELEASE_DEV",
                "CROS_BRYA_RELEASE_BETA",
                "CROS_BRYA_RELEASE_STABLE",
                "CROS_FIZZ_RELEASE_DEV",
                "CROS_FIZZ_RELEASE_BETA",
                "CROS_FIZZ_RELEASE_STABLE",
                "CROS_GUYBRUSH_RELEASE_DEV",
                "CROS_GUYBRUSH_RELEASE_BETA",
                "CROS_GUYBRUSH_RELEASE_STABLE",
                "CROS_PUFF_RELEASE_DEV",
                "CROS_PUFF_RELEASE_BETA",
                "CROS_PUFF_RELEASE_STABLE",
            ],
        ),
        "lacros_skylab_tests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_BRYA_RELEASE_LKGM",
                "CROS_FIZZ_RELEASE_LKGM",
                "CROS_GUYBRUSH_RELEASE_LKGM",
                "CROS_PUFF_RELEASE_LKGM",
            ],
        ),
        "lacros_skylab_tests_with_gtests_version_skew": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_BRYA_RELEASE_DEV",
                "CROS_BRYA_RELEASE_BETA",
                "CROS_BRYA_RELEASE_STABLE",
                "CROS_FIZZ_RELEASE_DEV",
                "CROS_FIZZ_RELEASE_BETA",
                "CROS_FIZZ_RELEASE_STABLE",
                "CROS_GUYBRUSH_RELEASE_DEV",
                "CROS_GUYBRUSH_RELEASE_BETA",
                "CROS_GUYBRUSH_RELEASE_STABLE",
                "CROS_PUFF_RELEASE_DEV",
                "CROS_PUFF_RELEASE_BETA",
                "CROS_PUFF_RELEASE_STABLE",
            ],
        ),
        "lacros_skylab_tests_with_gtests": targets.legacy_matrix_config(
            mixins = [
                "skylab-cft",
            ],
            variants = [
                "CROS_BRYA_RELEASE_LKGM",
                "CROS_FIZZ_RELEASE_LKGM",
                "CROS_GUYBRUSH_RELEASE_LKGM",
                "CROS_PUFF_RELEASE_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "lacros_skylab_tests_amd64_generic_rel",
    basic_suites = {
        "lacros_skylab_tests": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_LKGM",
            ],
        ),
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            variants = [
                "CROS_VOLTEER_PUBLIC_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "lacros_skylab_tests_amd64_generic_rel_gtest",
    basic_suites = {
        "chromeos_integration_tests_suite": targets.legacy_matrix_config(
            mixins = [
                "ci_only",
            ],
            variants = [
                "CROS_VOLTEER_PUBLIC_LKGM",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "lacros_skylab_tests_amd64_generic_rel_tast",
    basic_suites = {
        "lacros_skylab_tests": targets.legacy_matrix_config(
            mixins = [
                "ci_only",
            ],
            variants = [
                "CROS_VOLTEER_PUBLIC_LKGM",
            ],
        ),
    },
)

# This is:
#   linux_chromeos_gtests
#   - linux_chromeos_specific_gtests
#   + linux_chromeos_lacros_gtests
#   + linux_lacros_chrome_browsertests_version_skew
#   + linux_lacros_specific_gtests
targets.legacy_matrix_compound_suite(
    name = "linux_lacros_gtests",
    basic_suites = {
        "aura_gtests": None,
        "chromium_gtests": None,
        "chromium_gtests_for_devices_with_graphical_output": None,
        "chromium_gtests_for_linux_and_chromeos_only": None,
        "chromium_gtests_for_win_and_linux_only": None,
        "linux_chromeos_lacros_gtests": None,
        "linux_flavor_specific_chromium_gtests": None,
        "linux_lacros_specific_gtests": None,
        "non_android_chromium_gtests": None,
        "linux_lacros_chrome_browsertests_non_version_skew": None,
        "linux_lacros_chrome_browsertests_version_skew": targets.legacy_matrix_config(
            variants = [
                "LACROS_VERSION_SKEW_CANARY",
                "LACROS_VERSION_SKEW_DEV",
                "LACROS_VERSION_SKEW_BETA",
                "LACROS_VERSION_SKEW_STABLE",
            ],
        ),
        "linux_lacros_chrome_interactive_ui_tests_version_skew": targets.legacy_matrix_config(
            variants = [
                "LACROS_VERSION_SKEW_CANARY",
                "LACROS_VERSION_SKEW_DEV",
                "LACROS_VERSION_SKEW_BETA",
                "LACROS_VERSION_SKEW_STABLE",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "linux_optional_gpu_tests_rel_gpu_telemetry_tests",
    basic_suites = {
        "gpu_common_and_optional_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "LINUX_INTEL_UHD_630_STABLE",
                "LINUX_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_webcodecs_telemetry_test": targets.legacy_matrix_config(
            variants = [
                "LINUX_INTEL_UHD_630_STABLE",
                "LINUX_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_webgl2_conformance_gl_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "LINUX_INTEL_UHD_630_STABLE",
                "LINUX_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_webgl_conformance_gl_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "LINUX_INTEL_UHD_630_STABLE",
                "LINUX_NVIDIA_GTX_1660_STABLE",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "linux_optional_gpu_tests_rel_gtests",
    basic_suites = {
        "gpu_gles2_conform_gtests": targets.legacy_matrix_config(
            variants = [
                "LINUX_INTEL_UHD_630_STABLE",
                "LINUX_NVIDIA_GTX_1660_STABLE",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "mac_optional_gpu_tests_rel_gpu_telemetry_tests",
    basic_suites = {
        "gpu_common_and_optional_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
                "MAC_RETINA_NVIDIA_GPU_STABLE",
            ],
        ),
        "gpu_gl_passthrough_ganesh_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
            ],
        ),
        "gpu_metal_passthrough_ganesh_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
            ],
        ),
        "gpu_webcodecs_gl_passthrough_ganesh_telemetry_test": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
                "MAC_RETINA_NVIDIA_GPU_STABLE",
            ],
        ),
        "gpu_webcodecs_metal_passthrough_ganesh_telemetry_test": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
            ],
        ),
        "gpu_webcodecs_metal_passthrough_graphite_telemetry_test": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
            ],
        ),
        "gpu_webgl2_conformance_metal_passthrough_graphite_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
            ],
        ),
        "gpu_webgl_conformance_gl_passthrough_ganesh_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
            ],
        ),
        "gpu_webgl_conformance_metal_passthrough_ganesh_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
            ],
        ),
        "gpu_webgl_conformance_swangle_passthrough_representative_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "mac_optional_gpu_tests_rel_gtests",
    basic_suites = {
        "gpu_fyi_and_optional_non_linux_gtests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
                "MAC_RETINA_NVIDIA_GPU_STABLE",
            ],
        ),
        "gpu_fyi_mac_specific_gtests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
                "MAC_RETINA_NVIDIA_GPU_STABLE",
            ],
        ),
        "gpu_gles2_conform_gtests": targets.legacy_matrix_config(
            variants = [
                "MAC_MINI_INTEL_GPU_STABLE",
                "MAC_RETINA_AMD_GPU_STABLE",
                "MAC_RETINA_NVIDIA_GPU_STABLE",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "optimization_guide_desktop_gtests",
    basic_suites = {
        "optimization_guide_nogpu_gtests": None,
        "optimization_guide_gpu_gtests": None,
    },
)

targets.legacy_matrix_compound_suite(
    name = "optimization_guide_desktop_script_tests",
    basic_suites = {
        "model_validation_tests_suite": None,
        "model_validation_tests_light_suite": None,
        "ondevice_quality_tests_suite": None,
        "ondevice_stability_tests_suite": None,
    },
)

targets.legacy_matrix_compound_suite(
    name = "optimization_guide_linux_gtests",
    basic_suites = {
        "optimization_guide_nogpu_gtests": targets.legacy_matrix_config(
            mixins = [
                "gce",
            ],
        ),
        "optimization_guide_gpu_gtests": targets.legacy_matrix_config(
            # TODO(b:322815244): Add AMD variant once driver issues are fixed.
            variants = [
                "INTEL_UHD_630_OR_770",
                "NVIDIA_GEFORCE_GTX_1660",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "optimization_guide_linux_script_tests",
    basic_suites = {
        "model_validation_tests_suite": targets.legacy_matrix_config(
            mixins = [
                "gce",
            ],
        ),
        "model_validation_tests_light_suite": targets.legacy_matrix_config(
            mixins = [
                "gce",
            ],
        ),
        "ondevice_quality_tests_suite": targets.legacy_matrix_config(
            variants = [
                "INTEL_UHD_630_OR_770",
                "NVIDIA_GEFORCE_GTX_1660",
            ],
        ),
        "ondevice_stability_tests_suite": targets.legacy_matrix_config(
            variants = [
                "INTEL_UHD_630_OR_770",
                "NVIDIA_GEFORCE_GTX_1660",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "optimization_guide_win_gtests",
    basic_suites = {
        "optimization_guide_nogpu_gtests": targets.legacy_matrix_config(
            mixins = [
                "gce",
            ],
        ),
        "optimization_guide_gpu_gtests": targets.legacy_matrix_config(
            variants = [
                "AMD_RADEON_RX_5500_XT",
                "INTEL_UHD_630_OR_770",
                "NVIDIA_GEFORCE_GTX_1660",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "optimization_guide_win_script_tests",
    basic_suites = {
        "model_validation_tests_suite": targets.legacy_matrix_config(
            mixins = [
                "gce",
            ],
        ),
        "model_validation_tests_light_suite": targets.legacy_matrix_config(
            mixins = [
                "gce",
            ],
        ),
        "ondevice_quality_tests_suite": targets.legacy_matrix_config(
            variants = [
                "AMD_RADEON_RX_5500_XT",
                "INTEL_UHD_630_OR_770",
                "NVIDIA_GEFORCE_GTX_1660",
            ],
        ),
        "ondevice_stability_tests_suite": targets.legacy_matrix_config(
            variants = [
                "AMD_RADEON_RX_5500_XT",
                "INTEL_UHD_630_OR_770",
                "NVIDIA_GEFORCE_GTX_1660",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "webview_trichrome_10_cts_tests_gtest",
    basic_suites = {
        "webview_trichrome_cts_tests_suite": targets.legacy_matrix_config(
            variants = [
                "WEBVIEW_TRICHROME_FULL_CTS_TESTS",
                "WEBVIEW_TRICHROME_INSTANT_CTS_TESTS",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "webview_trichrome_64_cts_hostside_gtests",
    basic_suites = {
        "webview_trichrome_64_cts_hostside_tests_suite": targets.legacy_matrix_config(
            variants = [
                "WEBVIEW_TRICHROME_FULL_CTS_TESTS",
                "WEBVIEW_TRICHROME_INSTANT_CTS_TESTS",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "win_optional_gpu_tests_rel_gpu_telemetry_tests",
    basic_suites = {
        "gpu_common_and_optional_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_passthrough_graphite_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_webcodecs_telemetry_test": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_webgl2_conformance_d3d11_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_webgl_conformance_d3d11_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_webgl_conformance_d3d9_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_webgl_conformance_vulkan_passthrough_telemetry_tests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "win_optional_gpu_tests_rel_gtests",
    basic_suites = {
        "gpu_default_and_optional_win_media_foundation_specific_gtests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
            ],
        ),
        "gpu_default_and_optional_win_specific_gtests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_fyi_and_optional_non_linux_gtests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_fyi_and_optional_win_specific_gtests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
        "gpu_gles2_conform_gtests": targets.legacy_matrix_config(
            variants = [
                "WIN10_INTEL_UHD_630_STABLE",
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
    },
)

targets.legacy_matrix_compound_suite(
    name = "win_optional_gpu_tests_rel_isolated_scripts",
    basic_suites = {
        "gpu_command_buffer_perf_passthrough_isolated_scripts": targets.legacy_matrix_config(
            variants = [
                "WIN10_NVIDIA_GTX_1660_STABLE",
            ],
        ),
    },
)
