# Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/),
certificate by [SignPath Foundation](https://signpath.org/).

## What is signed

- `Foton-Setup.exe`, the installer for Windows.

It is built from the source code in this repository by the GitHub Actions workflow
[`build-windows.yml`](.github/workflows/build-windows.yml) on runners hosted by GitHub.
Files built on other computers are never signed.

`Foton-portable.7z`, the version that runs without installation, has the same files as the installer.
It is not signed, because SignPath signs files in ZIP archives, but not in 7z archives.

Foton includes third-party libraries, e.g. Qt and KDE Frameworks, as built by
[KDE Craft](https://community.kde.org/Craft). They are distributed unchanged and are not signed
with the certificate of SignPath Foundation.

## How signing works

1. The workflow builds Foton and uploads the files as the artifact `Foton-Windows`.
2. The artifact is submitted to SignPath. SignPath checks that it was built by this workflow
   from this repository and signs the files listed in
   [`.signpath/artifact-configuration.xml`](.signpath/artifact-configuration.xml).
3. Builds of tags (`v*`) use the signing policy `release-signing` and wait until an approver
   approves the request in SignPath. Other builds use `test-signing`.
4. The signed files are uploaded as the artifact `Foton-Windows-signed` and published
   on the Releases page.

Signing is turned on in the repository settings, Secrets and variables → Actions:

- variable `SIGNPATH_ORGANIZATION_ID`: the ID of the organization in SignPath;
- variable `SIGNPATH_PROJECT_SLUG`: the project in SignPath, `foton` if not set;
- secret `SIGNPATH_API_TOKEN`: the API token of a CI user in SignPath with permission to submit
  signing requests.

Without them, the files are built and published unsigned.

## Team roles and their members

- Committers and reviewers: [Dr-Livsi](https://github.com/Dr-Livsi)
- Approvers: [Dr-Livsi](https://github.com/Dr-Livsi)

Changes from other contributors are reviewed by a committer before they are merged.
Every signing of a release has to be approved by an approver.
All team members use multi-factor authentication for GitHub and SignPath.

## Privacy policy

This program will not transfer any information to other networked systems unless specifically
requested by the user or the person installing or operating it.
