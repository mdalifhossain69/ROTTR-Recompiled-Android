# Android release signing and updates

## Why Android rejected the update

Android only permits an in-place update when the new APK is signed by the same signing certificate as the installed APK. A build type named `release` does not guarantee a stable key.

The previous CI workflow generated a brand-new release keystore on every run. That changes the signing certificate from build to build, so Android reports `INSTALL_FAILED_UPDATE_INCOMPATIBLE (-7)` when a later APK is installed over an earlier one.

## One-time GitHub setup

Create one release keystore and keep it safe. Never commit it to this repository.

For example, on a trusted computer:

```bash
keytool -genkeypair -v -keystore rottr-release.p12 -storetype PKCS12 -alias rottr -keyalg RSA -keysize 2048 -validity 10000
base64 -w 0 rottr-release.p12
```

Add these repository Actions secrets under **Settings → Secrets and variables → Actions**:

- `RELEASE_KEYSTORE_BASE64`: the complete base64 output of that keystore
- `KEYSTORE_PASSWORD`: the keystore password
- `KEY_ALIAS`: `rottr` (or the alias you chose)
- `KEY_PASSWORD`: the key password

Keep a secure offline backup of the original keystore and passwords. Do not regenerate it for subsequent releases. If the original signing key is lost, APKs signed with a replacement key cannot update installations signed with the original key.

The CI workflow now restores this same keystore for each release and fails rather than silently generating a new key. It also increments `versionCode` using the GitHub Actions run number.

## Important: existing phone installation

This repository cannot recover the signing key from an installed app. If the currently installed ROTTR APK was signed by one of the old randomly generated CI keys, you must obtain that exact old keystore to update it in place. If that keystore was not saved, Android will not accept a different key: back up any app data you need, uninstall the existing app once, then install a release built with the persistent keystore configured above. After that one-time transition, future releases using the same keystore can update in place.

Do not uninstall if you still have the exact keystore that signed the installed APK; configure that key as the persistent secrets instead.

## Local release build

Use the same keystore and credentials as CI:

```bash
./gradlew assembleRelease \
  -PVERSION_CODE=2 \
  -PKEYSTORE_PATH=/secure/path/rottr-release.p12 \
  -PKEYSTORE_PASSWORD='...' \
  -PKEY_ALIAS=rottr \
  -PKEY_PASSWORD='...'
```

Never put real passwords or keystore files in source control, logs, or chat. Increase `VERSION_CODE` for each locally distributed update.
