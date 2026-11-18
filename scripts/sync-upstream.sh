#!/usr/bin/env bash
# Move the fork onto a newer upstream Nautilus release.
#
#   omarchy   an upstream release tag + the fork's commits
#
# Usage: scripts/sync-upstream.sh [TAG] [--no-push]
#
# Without TAG it picks the newest stable release in the series the fork is on
# now (51.1 → 51.2). Name a tag, such as 52.0, to move to the next GNOME
# release; do that once Arch ships the matching gtk4 and libadwaita.
#
# On a conflict the rebase stops: fix the files, `git add` them,
# `git rebase --continue`, then run this script again to test and push.
# rerere records each resolution, so the same conflict resolves itself next
# time. FORK.md lists every hook into upstream files.
#
# The whole body is one braced block: bash parses it before running, so the
# rebase can rewrite this file underneath it safely.
{
set -euo pipefail

UPSTREAM_URL=https://gitlab.gnome.org/GNOME/nautilus.git
FORK_BRANCH=omarchy
push=1
target=
for arg in "$@"; do
    case $arg in
        --no-push) push=0 ;;
        *) target=$arg ;;
    esac
done

cd "$(git rev-parse --show-toplevel)"

git remote get-url upstream >/dev/null 2>&1 || git remote add upstream "$UPSTREAM_URL"
git config rerere.enabled true
git config rerere.autoupdate true

if [[ -d .git/rebase-merge || -d .git/rebase-apply ]]; then
    echo "A rebase is in progress. Finish it (git rebase --continue) or abort it first." >&2
    exit 1
fi
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "Working tree has uncommitted changes; commit or stash them first." >&2
    exit 1
fi

git fetch --quiet --tags upstream

git switch --quiet "$FORK_BRANCH"
current=$(git describe --tags --abbrev=0 --match '[0-9]*' HEAD)
if [[ -z $target ]]; then
    series=${current%%.*}
    target=$(git tag --list "$series.*" --sort=-v:refname | grep -vE 'alpha|beta|rc' | head -n1)
fi
git rev-parse --quiet --verify "refs/tags/$target" >/dev/null || {
    echo "No upstream tag $target." >&2
    exit 1
}

echo "fork is on $current; target is $target"
if [[ $current != "$target" ]]; then
    newer=$(git tag --list "${target%%.*}.*" --sort=-v:refname | grep -vE 'alpha|beta|rc' | head -n1)
    [[ $newer != "$target" ]] && echo "note: $newer is newer than $target"

    # A safety net until the next successful sync.
    git branch -f "$FORK_BRANCH-before-sync" "$FORK_BRANCH"
    echo "rebasing $FORK_BRANCH onto $target (backup: $FORK_BRANCH-before-sync)"
    if ! git rebase --onto "$target" "$current" "$FORK_BRANCH"; then
        cat >&2 <<'MSG'

Conflict. Resolve it, then:
    git add <files> && git rebase --continue
    scripts/sync-upstream.sh
Or give up with `git rebase --abort`. FORK.md lists what each hook is for.
MSG
        exit 1
    fi
fi

echo "fork commits on top of $target:"
git log --oneline "$target..$FORK_BRANCH" | sed 's/^/  /'

echo "building and running the fork's tests"
if [[ ! -d build ]]; then
    meson setup build --prefix=/usr -Dselinux=disabled -Dtests=headless >/dev/null
fi
meson compile -C build >/dev/null
meson test -C build --print-errorlogs test-omarchy-palette

if [[ $push == 1 ]] && git remote get-url origin >/dev/null 2>&1; then
    git push --quiet --force-with-lease origin "$FORK_BRANCH"
    git push --quiet origin "refs/tags/$target"
    echo "pushed $FORK_BRANCH to origin"
fi
echo "now run ./install.sh to package and install it"
exit 0
}
