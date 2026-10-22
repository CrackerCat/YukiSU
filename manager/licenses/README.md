# Open-source licenses

The About page reads an offline inventory generated from these files:

- `components.json`: component rules keyed by Maven `group:artifact`, source
  links, license identifiers and references to retained notices. Versions come
  from each variant's resolved runtime dependencies.
- `texts/`: license texts and copyright notices, including notices for libraries
  bundled inside native code, AARs and JavaScript assets.
- `native-inputs.json`: paths of native build inputs and submodules to include
  in the generated provenance record, without duplicate hash/revision pins.

Gradle matches resolved runtime modules to the inventory and fails if a module's
license rule, notice, native input or initialized submodule is missing.
It also collects LICENSE, NOTICE, COPYING and COPYRIGHT resources from runtime
JARs and AARs, including nested JARs. Identical texts are stored once by SHA-256.
The generated catalog is packaged under `assets/open_source/` for each variant.
It covers dependencies before R8 shrinking; debug-only components are omitted
from Release.

Every build also generates `assets/open_source/inputs.json` with the exact
runtime coordinates and artifact SHA-256 values, native input SHA-256 values,
and checked-out submodule revisions. Native text hashes normalize CRLF to LF;
artifact hashes cover the original bytes. Gradle tracks these inputs, so changed
versions, files and revisions regenerate the catalog automatically, including
when the configuration cache is reused. These hashes describe the build inputs;
they are not an independent dependency authenticity check.

Components may list `sourceTexts` to collect current native LICENSE/NOTICE files
alongside retained notices, and `versionFrom` to use a submodule's current commit
as their displayed version. A `versionFrom` entry must use `{revision}` in its
source URL; Gradle substitutes the same full commit while preserving any path
suffix. Lua uses `mitLicenseFrom` to extract the complete MIT license comment
from the current `lua.h`. A missing or ambiguous license comment fails generation.
Every source text must be in `native-inputs.json` so Gradle tracks it.
LZ4 and its bundled xxHash use the revision of
`userspace/ksud/third_party/MagiskbootAlone/external/lz4`; SipHash uses the libzip
submodule revision. Keep these nested sources in the input list when updating
their parent dependency.
Retained notices also cover shaded libraries, JavaScript and
source imports that cannot be identified from Maven metadata alone.

## Updating dependencies

For ordinary upgrades, change only the dependency version (or merge the
Dependabot version change) and build normally. No hash refresh, generated-file
commit or CI write-back step is needed. Both `assemble` and `bundle` wire license
generation into their assets automatically.

When adding a new module, add its version-independent `group:artifact` rule and
license notices once. When adding native inputs, add their paths rather than
hashes. Initialize submodules with `git submodule update --init --recursive`.
Review actual license changes and newly bundled native, JavaScript or data
components; generation does not infer licensing terms for unknown components.

To check both variants explicitly, run from `manager/`:

```text
gradlew.bat :app:generateReleaseOpenSourceLicenses :app:generateDebugOpenSourceLicenses :app:assembleRelease :app:lintDebug :app:lintRelease
```

Check the resulting APK's catalog and every referenced text. Keep release source
and build materials aligned with the shipped binaries and recursive submodule
revisions; the in-app notices are only one part of distribution compliance.

WebUI X's bundled JavaScript shims and custom editor lack exact package-version
metadata. Their entries identify the reviewed WebUI X bundle revision; retain
that distinction until the upstream package metadata is available.

The generator's fixture checks exercise upgrades, changed hashes and notices,
variant filtering, and missing-license failures without downloading dependencies:

```text
gradlew.bat -I licenses/tests/generation.init.gradle :app:checkLicenseGeneration --no-configuration-cache
```
