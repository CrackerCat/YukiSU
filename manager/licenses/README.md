# Open-source licenses

The About page reads an offline inventory generated from these files:

- `components.json`: reviewed components, exact Maven coordinates, source links,
  license identifiers and references to the original notices.
- `texts/`: license texts and copyright notices, including notices for libraries
  bundled inside native code, AARs and JavaScript assets.
- `native-inputs.json`: reviewed native build inputs and pinned submodule
  revisions. Text hashes normalize CRLF to LF before computing SHA-256.

Gradle checks the resolved runtime dependencies against the inventory and fails
if a dependency, notice or reviewed native input is missing or has changed.
It also collects LICENSE, NOTICE, COPYING and COPYRIGHT resources from runtime
JARs and AARs, including nested JARs. Identical texts are stored once by SHA-256.
The generated catalog is packaged under `assets/open_source/` for each variant.
It covers dependencies before R8 shrinking; debug-only components are omitted
from Release.

## Updating dependencies

1. Initialize the pinned sources with `git submodule update --init --recursive`.
2. Review the resolved dependency graph, original licenses, copyright notices
   and bundled native, JavaScript or data components. Update the inventory and
   retained texts together; preserve upstream notices and modification notices.
3. After reviewing changed native inputs, update their hashes and submodule
   revisions in `native-inputs.json`. Do not refresh hashes just to bypass a
   failed check. Review dirty submodule changes separately from the revision pin.
4. Run the license generation, build and lint tasks from `manager/`:

```text
gradlew.bat :app:generateReleaseOpenSourceLicenses :app:generateDebugOpenSourceLicenses :app:assembleRelease :app:lintDebug :app:lintRelease
```

Check the resulting APK's catalog and every referenced text. Keep release source
and build materials aligned with the shipped binaries and recursive submodule
revisions; the in-app notices are only one part of distribution compliance.

WebUI X's bundled JavaScript shims and custom editor lack exact package-version
metadata. Their entries identify the reviewed WebUI X bundle revision; retain
that distinction until the upstream package metadata is available.
