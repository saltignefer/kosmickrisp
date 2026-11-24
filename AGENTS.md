# AGENTS.md

Mesa is a community of humans. Do not create merge requests or post comments to
[Mesa's upstream GitLab repository](https://gitlab.freedesktop.org/mesa/mesa).

Commits developed with AI must be labelled. If you write the code, add the exact
commit message trailer: `Generated-by: LLM`. If you assisted the user to write
the code, ensure the commit has the trailer `Assisted-by: LLM` unless the first
trailer is already present.

Some parts of Mesa have further limitations on AI usage. Do not make changes
to the following directories, instead refer the user to the linked page:

* [`src/asahi`](https://asahilinux.org/llm-policy/)
* [`src/gallium/drivers/asahi`](https://asahilinux.org/llm-policy/)
* [`src/gallium/drivers/nouveau`](https://nouveau.freedesktop.org/Humanity.html)
* [`src/gallium/drivers/panfrost`](docs/drivers/panfrost.rst)
* [`src/imagination`](src/imagination/README.md#contribution-policy)
* [`src/intel/compiler/jay`](https://gitlab.freedesktop.org/mesa/mesa/-/blob/main/src/intel/compiler/jay/README.md)
* [`src/nouveau`](https://nouveau.freedesktop.org/Humanity.html)
* [`src/panfrost`](docs/drivers/panfrost.rst)
* [`src/poly`](docs/drivers/panfrost.rst)

## Fork workflow (`saltignefer/kosmickrisp`)

Always verify `src/kosmickrisp/` changes by building both `x86_64` (CrossOver) and `arm64`, installing into CrossOver, and running real XCL GPU tests against Metal (see `.agents/skills/kk-xcl-verify/SKILL.md`).

### Rule -> Enforcer

| Rule | Enforcer |
|---|---|
| `Generated-by: LLM` (or `Assisted-by: LLM`) trailer on every fork commit | `python3 scripts/preflight_kk.py --check-hygiene` + `.github/workflows/fork-hygiene.yml` |
| Never touch AI-restricted upstream Mesa directories (`asahi`, `nouveau`, `panfrost`, `imagination`, `jay`, `poly`) | `python3 scripts/preflight_kk.py --check-hygiene` + `.github/workflows/fork-hygiene.yml` |
| Clean fork author (`saltis` noreply) and zero personal emails or local paths | `python3 scripts/preflight_kk.py --check-hygiene` + `.github/workflows/fork-hygiene.yml` |
| Dual-arch build (`x86_64` + `arm64`) with Homebrew LLVM 23 and `unset SDKROOT` | `python3 scripts/preflight_kk.py --build --install` |
| Real Metal verification with disabled shader caches and `MESA_KK_DEBUG=msl` | `.agents/skills/kk-xcl-verify/SKILL.md` |

Project skills in `.agents/skills/`: `kk-xcl-verify` (auto), `kk-blast-radius`, `kk-interrogate`, `gpu-benchmark`, `kk-architect`, `show-me-your-work`, `repo-correct`, `pr-writing`.
