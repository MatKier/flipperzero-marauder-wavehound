# TODO / ideas

Loose, non-committal notes. Nothing here is planned work — just things we
might do.

## Maybe

- **Bundle `oui.db` into the `.fap` for catalog builds.** The catalog builds
  with `ufbt` on the committed tree and never runs `build_app.py`, so the
  vendor DB is currently absent from a catalog-built app (it's gitignored and
  generated locally). We could ship it out of the box the way
  [keetsta/wol-flipper](https://github.com/keetsta/wol-flipper) bundles its
  ESP32 firmware: drop `oui.db` from `assets/.gitignore` and commit
  the real file, so `fap_file_assets="assets"` packs it. Trade-offs: a ~1.25 MB
  binary blob in git history (consider trimming the DB first), and an
  attribution/NOTICE for the Wireshark `manuf`-derived data. Until then, users
  can drop their own `oui.db` into `/ext/apps_data/wavehound/`.
