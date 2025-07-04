// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use chrono::Datelike;
use file_header::{check_headers_recursively, license::spdx::*};
use std::path;

pub(crate) fn check_license_headers(root: &path::Path) -> anyhow::Result<()> {
    log::info!("Checking license headers");
    let ignore = license_ignore()?;
    let results = check_headers_recursively(
        root,
        |p| !ignore.is_match(p),
        APACHE_2_0.build_header(YearCopyrightOwnerValue::new(
            u32::try_from(chrono::Utc::now().year())?,
            "Google LLC".to_string(),
        )),
        4,
    )?;

    for path in results.no_header_files.iter() {
        eprintln!("Header not present: {path:?}");
    }

    for path in results.binary_files.iter() {
        eprintln!("Binary file: {path:?}");
    }
    if !results.binary_files.is_empty() {
        eprintln!("Consider adding binary files to the ignore list in src/licence.rs.");
    }

    if results.has_failure() {
        Err(anyhow::anyhow!("License header check failed"))
    } else {
        Ok(())
    }
}

pub(crate) fn add_license_headers(root: &path::Path) -> anyhow::Result<()> {
    let ignore = license_ignore()?;
    for p in file_header::add_headers_recursively(
        root,
        |p| !ignore.is_match(p),
        APACHE_2_0.build_header(YearCopyrightOwnerValue::new(
            u32::try_from(chrono::Utc::now().year())?,
            "Google LLC".to_string(),
        )),
    )? {
        println!("Added header: {:?}", p);
    }

    Ok(())
}

fn license_ignore() -> Result<globset::GlobSet, globset::Error> {
    let mut builder = globset::GlobSetBuilder::new();
    for lic in license_ignore_dirs() {
        builder.add(globset::Glob::new(lic)?);
    }
    builder.build()
}

fn license_ignore_dirs() -> Vec<&'static str> {
    // These will be checked against the absolute path of each file.
    vec![
        "**/android/build/**",
        "**/target/**",
        "**/.idea/**",
        "**/cmake-build/**",
        "**/java/build/**",
        "**/java/*/build/**",
        "**/ukey2_c_ffi/cpp/build/**",
        "**/*.toml",
        "**/*.md",
        "**/*.lock",
        "**/*.json",
        "**/*.rsp",
        "**/*.patch",
        "**/*.dockerignore",
        "**/*.apk",
        "**/gradle/*",
        "**/.gradle/*",
        "**/.git*",
        "**/*test*vectors.txt",
        "**/auth_token.txt",
        "**/*.mdb",
        "**/.DS_Store",
        "**/fuzz/corpus/**",
        "**/.*.swp",
        "**/*.vim",
        "**/*.properties",
        "**/third_party/**",
        "**/*.png",
        "**/*.ico",
        "**/node_modules/**",
        "**/.angular/**",
        "**/.editorconfig",
        "**/*.class",
        "**/fuzz/artifacts/**",
        "**/cmake-build-debug/**",
        "**/tags",
    ]
}

#[cfg(test)]
mod tests {
    #[test]
    fn new_ignore_is_likely_buggy() {
        for dir in super::license_ignore_dirs() {
            assert!(dir.starts_with("**/"), "Matching on the root filesystem is likely unintended");
        }
    }
}
