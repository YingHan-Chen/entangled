workspace(name="org_iota_entangled")

git_repository(
    name="rules_iota",
    remote="https://gitlab.com/iota-foundation/software/rules_iota.git",
    commit="2ebb283b3ca02431291047b1f99e168449f97d66")

android_sdk_repository(
    name="androidsdk",
    api_level=16, )

android_ndk_repository(
    name="androidndk",
    api_level=14, )

load("@rules_iota//:defs.bzl", "iota_deps")
iota_deps()