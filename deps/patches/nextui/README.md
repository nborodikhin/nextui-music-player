# Patches for the NextUI checkout

`./dev deps fetch` applies each `*.patch` here, in name order, to every
dependency whose `deps.json` entry names this directory. The pin marker in the
checkout records the result, thus a build tells a patched tree from a tree with
hand edits.

Ideally, each patch should have an open pull request upstream. Keep the file
the same as the diff of the pull request. Remove the patch when the pull
request is merged and the pin moves to a revision that contains it.

| Patch | Upstream |
|---|---|

Refresh a patch from its upstream pull request. For example:

```bash
gh pr diff 828 --repo LoveRetro/NextUI > deps/patches/nextui/0001-plat-capturescreenshot.patch
./dev deps fetch --force
```
