import sys

def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "dxb/build.sh"
    with open(target, "r", encoding="utf-8") as f:
        content = f.read()

    # 1. libfirehose_kernel arm64 support
    content = content.replace(
        'ARCHS="x86_64 arm64e" VALID_ARCHS="x86_64 arm64e"',
        'ARCHS="x86_64 arm64e arm64" VALID_ARCHS="x86_64 arm64e arm64"'
    )

    # 2. Strict patch application loop (no skipping!)
    old_loop = """        for PATCH in "${PATCH_DIR}"/*.patch; do
            if APPLY_CHECK=$(git apply --check "$PATCH" 2>&1); then
                running "Applying patch: ${PATCH}"
                git apply "$PATCH"
            else
                warning "Skipping patch ${PATCH} (already upstream, previously applied, or malformed): ${APPLY_CHECK}"
            fi
        done"""

    new_loop = """        set -e
        for PATCH in "${PATCH_DIR}"/*.patch; do
            running "Applying patch: ${PATCH}"
            git apply -v "${PATCH}"
        done"""

    if old_loop in content:
        content = content.replace(old_loop, new_loop)
        print("[OK] Replaced patch loop with strict set -e git apply -v")
    else:
        print("[WARN] old_loop not found directly in content")

    with open(target, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[OK] Patched {target}")

if __name__ == "__main__":
    main()
