# Patches for the NextUI checkout

`./dev deps fetch` applies each `*.patch` here, in name order, to every
dependency whose `deps.json` entry names this directory. The pin marker in the
checkout records the result, thus a build tells a patched tree from a tree with
hand edits.

Each patch is a pull request that is open upstream. Keep the file the same as
the diff of the pull request, thus the patch goes away with no change to this
project when the pull request is merged and the pin moves.

| Patch | Upstream |
|---|---|
| `0001-plat-capturescreenshot.patch` | [LoveRetro/NextUI#828](https://github.com/LoveRetro/NextUI/pull/828) - `PLAT_captureScreenshot()` |

Refresh one with:

```bash
gh pr diff 828 --repo LoveRetro/NextUI > deps/patches/nextui/0001-plat-capturescreenshot.patch
./dev deps fetch --force
```
