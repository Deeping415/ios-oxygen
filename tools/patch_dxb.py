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

    # 2. Target-aware patch application loop (fail on critical Vince patches, warn on optional)
    old_loop = """        for PATCH in "${PATCH_DIR}"/*.patch; do
            if APPLY_CHECK=$(git apply --check "$PATCH" 2>&1); then
                running "Applying patch: ${PATCH}"
                git apply "$PATCH"
            else
                warning "Skipping patch ${PATCH} (already upstream, previously applied, or malformed): ${APPLY_CHECK}"
            fi
        done"""

    new_loop = """        for PATCH in "${PATCH_DIR}"/*.patch; do
            if APPLY_CHECK=$(git apply --check "$PATCH" 2>&1); then
                running "Applying patch: ${PATCH}"
                git apply "$PATCH"
            else
                case "${PATCH}" in
                    *0004*|*0090*|*vince*)
                        error "CRITICAL VINCE PATCH FAILED: ${PATCH}: ${APPLY_CHECK}"
                        exit 1
                        ;;
                    *)
                        warning "Skipping optional patch ${PATCH}: ${APPLY_CHECK}"
                        ;;
                esac
            fi
        done"""

    if old_loop in content:
        content = content.replace(old_loop, new_loop)
        print("[OK] Replaced patch loop with target-aware Vince check")
    else:
        print("[WARN] old_loop not found directly in content")

    with open(target, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[OK] Patched {target}")

if __name__ == "__main__":
    main()
