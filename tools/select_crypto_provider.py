"""Select exactly one Arduino Crypto provider for MeshInk protocol cores.

Both upstream cores declare a library named "Crypto":
- MeshCore declares rweather/Crypto.
- libmeshtastic-leaf declares Meshtastic's fork.

PlatformIO installs transitive dependencies before pre-scripts run, so when both
protocols are enabled it may materialize both same-named packages and later link
both archives. That produces duplicate symbols even though the Meshtastic fork
is API-compatible with the AES128 + SHA256/HMAC subset MeshCore uses.

The project-level [meshink-crypto] selector is authoritative. This pre-script
removes only the competing installed Crypto package from the per-environment
libdeps directory before LDF/building. It never modifies either upstream core.

If Leaf is removed/replaced, changing [meshink-crypto].lib_deps to
${meshink-crypto-meshcore.lib_deps} restores MeshCore's original provider.
"""

from pathlib import Path
import shutil

Import("env")

deps = env.GetProjectOption("lib_deps", [])
if isinstance(deps, str):
    dep_text = deps
else:
    dep_text = "\n".join(str(item) for item in deps)

want_meshtastic = (
    "meshtastic/Crypto" in dep_text
    or "591ff9a690e8168ccb7a36abde8d7783e448d395" in dep_text
)
want_meshcore = "rweather/Crypto" in dep_text

if want_meshtastic == want_meshcore:
    raise RuntimeError(
        "MeshInk Crypto selector must resolve to exactly one provider "
        "(Meshtastic fork or rweather/Crypto)"
    )

env_name = env.subst("$PIOENV")
libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env_name

if not libdeps.is_dir():
    raise RuntimeError(f"MeshInk libdeps directory missing: {libdeps}")

crypto_dirs = [
    path
    for path in libdeps.iterdir()
    if path.is_dir()
    and path.name.startswith("Crypto")
    and (path / "AES.h").is_file()
    and (path / "SHA256.h").is_file()
]

if not crypto_dirs:
    raise RuntimeError(
        f"MeshInk selected Crypto provider was not installed under {libdeps}"
    )

kept = []
removed = []
for path in crypto_dirs:
    is_meshtastic = (path / "XEdDSA.h").is_file()
    keep = is_meshtastic if want_meshtastic else not is_meshtastic
    if keep:
        kept.append(path)
    else:
        shutil.rmtree(path)
        removed.append(path.name)

if len(kept) != 1:
    raise RuntimeError(
        "MeshInk expected exactly one selected Crypto package; "
        f"kept={[p.name for p in kept]}, candidates={[p.name for p in crypto_dirs]}"
    )

provider = "Meshtastic fork" if want_meshtastic else "rweather/Crypto"
suffix = f"; removed {', '.join(removed)}" if removed else ""
print(f"[MeshInk] Crypto provider: {provider} ({kept[0].name}){suffix}")
