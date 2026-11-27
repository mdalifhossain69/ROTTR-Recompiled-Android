# Android debug builds

This repository intentionally builds and distributes **debug APKs only**. There is no release signing configuration, no release keystore requirement, and no release APK workflow.

## Build locally

From the `android/` directory:

```bash
./gradlew assembleDebug -PVERSION_CODE=1
```

The APK is written to:

```
android/build/outputs/apk/debug/
```

## GitHub Actions

Run **Build ROTTR Android Debug APK** from the Actions tab. The workflow builds `assembleDebug`, verifies the APK signature, and uploads a `rottr-recomp-debug-apk` artifact. No repository secrets are required.

GitHub-hosted runners are temporary, so the workflow caches its generated Android debug keystore under a fixed cache key. Successive manually dispatched builds can normally reuse the same debug signing certificate while that cache remains available. Pull-request builds may use a temporary key and should not be treated as update-compatible distribution builds.

## Updating an existing installation

Android permits an in-place update only when the signing certificate matches and the new APK has a compatible version code.

The CI debug signing cache preserves the key for successive manual builds. GitHub Actions caches are not permanent storage; if the cache is deleted or evicted, the next CI build may receive a different debug certificate and Android may require a one-time uninstall/reinstall. Back up the CI debug keystore if guaranteed long-term update compatibility is required.

The Android SDK's local debug keystore is separate from the GitHub Actions CI debug keystore. Local builds update installations signed with that same local key, not necessarily APKs downloaded from CI.

Debug APKs are intended for development and testing, not public production distribution.
