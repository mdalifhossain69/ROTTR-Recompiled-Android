# Android debug builds

The Android module is configured for development/debug APK builds. This document does not claim that the current source has been built successfully.

## Build locally

From the `android/` directory:

```bash
./gradlew assembleDebug -PVERSION_CODE=1
```

The APK is normally written to:

```
android/build/outputs/apk/debug/
```

Use a compatible Android SDK, NDK and JDK as required by the Gradle project.

## GitHub Actions policy

The repository workflow is intended for **manual dispatch only** from the Actions tab. Push and pull-request triggers are not wanted and must not be added. Do not start a workflow automatically when editing documentation or pushing commits.

If a manual workflow is run, inspect its actual run result and artifact before reporting a successful build. This document does not assert that an APK was built during the latest documentation update.

## APK update compatibility

Android allows an in-place update only when the signing certificate matches and the version code is compatible. A local Android debug keystore and a CI-generated debug keystore are different signing identities unless deliberately configured to match. Do not promise update compatibility without verifying the certificate used for both APKs.

Debug APKs are for development and testing, not production distribution.
