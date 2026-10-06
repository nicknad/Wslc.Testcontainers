# Branching and releases

Three kinds of branches exist:

| Branch | Purpose | Who pushes |
| --- | --- | --- |
| `main` | Released state. Every commit is a release or a release-ready merge. | Release PRs from `development` only |
| `development` | Integration branch. All 0.2.0 work lands here. | Merged PRs; direct pushes while bootstrapping |
| `<topic>` | Short-lived feature/fix branches. | Their author |

## `main` is pull-request-only

The `main-protection` ruleset (`github.com/nicknad/Wslc.Testcontainers/rules`) is active with no
bypass actors:

- deletions and force-pushes are rejected;
- changes must go through a pull request;
- two required status checks must pass:
  - `guard-source-branch` — enforces that the PR **source branch is `development`**;
  - `packages` — the `ci.yml` aggregate that depends on the full C# (net8/9/10), ARM64,
    Linux-guard, C++ build/test, clang-format/clang-tidy, and AddressSanitizer jobs.

GitHub has no native "pull requests only from branch X" rule, so
[`.github/workflows/main-source-guard.yml`](../.github/workflows/main-source-guard.yml) fails any
PR targeting `main` whose head branch is not `development`. The ruleset then blocks the merge
because the check never succeeds. If you ever need to relax the flow, edit the workflow and the
ruleset together.

Direct pushes to `main` fail with `GH013: Repository rule violations`:

```
remote: - Changes must be made through a pull request.
remote: - 2 of 2 required status checks are expected.
```

## `development` is history-preserving

The `development-protection` ruleset blocks deletions and force-pushes but allows direct pushes,
so short-lived topic branches can be merged without a second approval gate. Release PRs use a
merge commit (`gh pr merge --merge`) so `development` commits stay ancestors of `main`.

## Feature work

1. Branch from `development`: `git checkout -b module/valkey development`.
2. Open a PR against `development`. CI (`ci.yml`) runs on every PR and push.
3. Merge once green. Delete the topic branch.

Dependabot is configured with `target-branch: development` (both nuget and github-actions), so its
PRs never trip the main guard.

## Releases

1. Freeze the version: update `csharp/Directory.Build.props` (`VersionPrefix`, numeric part) and
   `cpp/CMakeLists.txt` (`project(... VERSION ...)`) in lockstep, and move the `CHANGELOG.md`
   `Unreleased` notes under the release heading. The numeric lockstep is enforced by
   `csharp/scripts/Verify-Package.ps1`.
2. Promote each assembly's `PublicAPI.Unshipped.txt` entries into `PublicAPI.Shipped.txt` (the
   `Verify-Package.ps1` promotion gate refuses to ship an unfrozen surface).
3. Open `development -> main`, wait for `guard-source-branch` and `packages`, merge with
   `gh pr merge --merge`.
4. Tag the merge commit on `main` (`git tag v0.2.0-preview.1 && git push origin v0.2.0-preview.1`).
   The `v*` tag ruleset blocks updating or deleting published tags. `release.yml` then packs
   `Wslc.Testcontainers` plus every module package and pushes to NuGet.org (trusted publishing).

## Changing the rules

The applied rulesets are versioned as JSON under [`.github/rulesets/`](rulesets/). To change
protection, edit the matching file and re-apply:

```powershell
gh api repos/nicknad/Wslc.Testcontainers/rulesets -X POST --input .github/rulesets/main-protection.json
# or update an existing ruleset:
gh api repos/nicknad/Wslc.Testcontainers/rulesets/<id> -X PUT --input .github/rulesets/main-protection.json
```

Repository administrators can always edit or delete a ruleset, so the guard is a process
mechanism backed by CI, not an account-level lock.
