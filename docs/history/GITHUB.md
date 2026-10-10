# Publish the source and player release to GitHub

Publish the unpacked project files as a repository so changes can be reviewed. Keep the compiled player ZIP as a Release attachment. A ZIP uploaded as a source file does not provide a useful source history.

## New, empty repository ¡ª Windows cmd.exe
Install Git for Windows if `git --version` is not recognized. On GitHub, create an empty repository named DynamicCubemapF4 under your account, choosing Public if desired. Leave GitHub's README, license and .gitignore initialization unchecked: this project already contains those files. Replace YOUR_USERNAME below with your account name.

```bat
cd /d "E:\Coding\F4\DCF4_Universal_1.3.1_Public"
git init -b main
git add .
git status
git commit -m "Release DynamicCubemapF4 1.3.1"
git remote add origin https://github.com/YOUR_USERNAME/DynamicCubemapF4.git
git push -u origin main
git tag -a v1.3.1 -m "DynamicCubemapF4 1.3.1 baseline"
git push origin v1.3.1
```

Inspect `git status` before committing: source, tests, licenses, MCM configuration and documentation should be included; build outputs, logs and personal settings should not. The supplied .gitignore handles common generated files. Configure `git config user.name "YOUR NAME"` and `git config user.email "YOUR GITHUB EMAIL"` in this folder if Git asks for identity. A GitHub-provided no-reply email is suitable. For HTTPS authentication, use Git Credential Manager's sign-in prompt; do not put tokens in commands or files.

## Existing repository
Clone your existing repository into its own local folder, copy these project contents into it, review `git diff`/`git status`, then commit and push. Remove obsolete plugin-owned source files such as Source/src/PluginVersion.cpp when replacing the earlier layout. Do not initialize a second repository or force-push over existing history. Keep the bundled dependency and license files. If a v1.3.1 tag already exists, inspect it before choosing how to proceed; do not overwrite it automatically.

## Publish the downloadable mod
Build and test the DLL, then run PACKAGE.cmd to create dist\DynamicCubemapF4-1.3.1.zip. On the repository's Releases page, draft a release using tag v1.3.1, paste the 1.3.1 changelog and attach that player ZIP. GitHub supplies a source archive for the tag automatically. You may also attach this source package. Review the assets, then publish when ready.

Future changes should use a branch, for example `git switch -c feature/next-change`, while retaining v1.3.1 as the baseline reference. Nothing has been pushed automatically by this package.

Official instructions:
- https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github
- https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository
