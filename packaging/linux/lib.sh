#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# lib.sh — shared helpers for the Linux packaging scripts.
#
# The first argument to a packaging script is normally the built ELF executable,
# but a CMake build root (the directory passed to `cmake -B`) is also accepted
# for convenience. This keeps the CI invocation unambiguous (the explicit binary
# is passed) while tolerating the historical `build`-directory form, which used
# to reach `install` as a directory and abort packaging.
# -----------------------------------------------------------------------------

# rrs_resolve_binary <path> — echo the resolved path to the built executable.
#
# Accepts either:
#   * a file path (typically the executable), returned as-is; or
#   * a build-root directory, searched for the standard CMake artefact paths.
#
# Returns non-zero with a diagnostic on failure.
rrs_resolve_binary() {
    local input="${1:?rrs_resolve_binary: missing path}"

    if [[ -f "${input}" ]]; then
        printf '%s\n' "${input}"
        return 0
    fi

    if [[ -d "${input}" ]]; then
        # Standard CMake layout first, then looser fallbacks.
        local candidates=(
            "${input}/raw_radio_studio_artefacts/Release/raw-radio-studio"
            "${input}/Release/raw-radio-studio"
            "${input}/raw-radio-studio"
        )
        local candidate
        for candidate in "${candidates[@]}"; do
            if [[ -f "${candidate}" ]]; then
                printf '%s\n' "${candidate}"
                return 0
            fi
        done
        echo "error: no raw-radio-studio executable found under build root: ${input}" >&2
        echo "       expected: raw_radio_studio_artefacts/Release/raw-radio-studio" >&2
        return 1
    fi

    echo "error: first argument is neither a file nor a directory: ${input}" >&2
    return 1
}
