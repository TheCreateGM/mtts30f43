#!/usr/bin/env bash
#
# Driver/setup-musa.sh
# MUSA SDK extraction, MTT_lib dependency resolution, and ordered component build
# for the Moore Threads MTT S30 GPU on Fedora Linux.
#
# Usage: sudo Driver/setup-musa.sh [--musa-home PATH] [--skip-torch] [--dry-run]
#
set -euo pipefail

# ── Root check — must happen before any system files are touched ──────────────
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: must run as root (sudo)"
    exit 1
fi

# ── Script location ───────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Defaults ──────────────────────────────────────────────────────────────────
MUSA_HOME="/usr/local/musa/"
DRY_RUN=false
SKIP_TORCH=false

# ── Global state set by extract_musa_sdk ─────────────────────────────────────
DEB_SOURCE=""
SDK_VERSION=""

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --musa-home)
            if [[ -z "${2:-}" ]]; then
                echo "ERROR: --musa-home requires a PATH argument"
                exit 1
            fi
            MUSA_HOME="$2"
            shift 2
            ;;
        --skip-torch)
            SKIP_TORCH=true
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        -h|--help)
            echo "Usage: sudo $0 [--musa-home PATH] [--skip-torch] [--dry-run]"
            echo ""
            echo "Options:"
            echo "  --musa-home PATH   Override MUSA installation prefix (default: /usr/local/musa/)"
            echo "  --skip-torch       Skip torch_musa build step"
            echo "  --dry-run          Print actions without executing them"
            exit 0
            ;;
        *)
            echo "ERROR: Unknown option: $1"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

# ── Logging helpers ───────────────────────────────────────────────────────────
log_info() {
    echo "[INFO]  $*"
}

log_warn() {
    echo "[WARN]  $*" >&2
}

log_error() {
    echo "[ERROR] $*" >&2
}

# ── Dry-run command helper ────────────────────────────────────────────────────
# Usage: dry_run_cmd <cmd> [args...]
# In dry-run mode: prints "[DRY-RUN] <cmd> [args...]" without executing.
# Otherwise: executes the command normally.
dry_run_cmd() {
    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] $*"
    else
        "$@"
    fi
}

# ── Existing installation check ──────────────────────────────────────────────
# Detects a prior MUSA installation at ${MUSA_HOME} and prompts for overwrite.
check_existing_installation() {
    # Consider an existing installation present if the state file OR mcc binary exists
    if [[ -f "${MUSA_HOME%/}/.musa-install-state.json" ]] || \
       [[ -f "${MUSA_HOME%/}/bin/mcc" ]]; then
        if [[ "${DRY_RUN}" == true ]]; then
            echo "[DRY-RUN] Would prompt: MUSA installation found at ${MUSA_HOME}. Overwrite? [y/N]"
        else
            read -r -p "MUSA installation found at ${MUSA_HOME}. Overwrite? [y/N] " answer
            if [[ "${answer}" != "y" && "${answer}" != "Y" ]]; then
                echo "Aborting."
                exit 0
            fi
        fi
    fi
}

# ── MTT_lib component classification ─────────────────────────────────────────
# Hard-coded lookup table for all 57 MTT_lib repositories.
# Tiers: core (3), ml-framework (3), application (51).
declare -A COMPONENT_TIERS=(
    # core
    ["muThrust"]="core"
    ["muAlg"]="core"
    ["mutlass"]="core"
    # ml-framework
    ["torch_musa"]="ml-framework"
    ["paddle_musa"]="ml-framework"
    ["tensorflow_musa_extension"]="ml-framework"
    # application (51 remaining repos)
    ["AI_Agent"]="application"
    ["audio"]="application"
    ["axInfrastructure"]="application"
    ["csi-driver-3fs"]="application"
    ["DeskVision"]="application"
    ["dynolog"]="application"
    ["FFmpeg"]="application"
    ["gpu-compute-driver-bench"]="application"
    ["installer-framework"]="application"
    ["kineto"]="application"
    ["LiteGS"]="application"
    ["mate"]="application"
    ["MobiMaliangSDK"]="application"
    ["MooER"]="application"
    ["Moore-AnimateAnyone"]="application"
    ["MT-DeepEP"]="application"
    ["MT-DeepSeek"]="application"
    ["MT-DualPipe"]="application"
    ["MT-flashMLA"]="application"
    ["MT-MegatronLM"]="application"
    ["MT-TransformerEngine"]="application"
    ["mtai-sdk-ts"]="application"
    ["mthreads-ml-py"]="application"
    ["mtImageCodec"]="application"
    ["mujoco_musa"]="application"
    ["mujoco_warp_musa"]="application"
    ["ollama-musa"]="application"
    ["opencv"]="application"
    ["opencv_contrib"]="application"
    ["PaddleCustomDevice"]="application"
    ["pytorch_cluster"]="application"
    ["pytorch_scatter"]="application"
    ["pytorch_sparse"]="application"
    ["pytorch-lightning"]="application"
    ["pytorch3d"]="application"
    ["qtbase"]="application"
    ["RetinaGS"]="application"
    ["SimuMax"]="application"
    ["StableGS"]="application"
    ["text2world"]="application"
    ["tilelang_musa"]="application"
    ["torchada"]="application"
    ["torchcodec"]="application"
    ["TurboRAG"]="application"
    ["TurboSplat-Viz"]="application"
    ["tutorial_on_musa"]="application"
    ["tvm_musa"]="application"
    ["tvm-ffi"]="application"
    ["usr"]="application"
    ["vision"]="application"
    ["vllm-musa"]="application"
)

# Prerequisites for each component (space-separated list)
declare -A COMPONENT_PREREQS=(
    ["muThrust"]="MUSA_SDK"
    ["muAlg"]="MUSA_SDK muThrust"
    ["mutlass"]="MUSA_SDK"
    ["torch_musa"]="MUSA_SDK muThrust muAlg"
    ["paddle_musa"]="MUSA_SDK muThrust"
    ["tensorflow_musa_extension"]="MUSA_SDK"
    # all application-tier repos have no explicit prereqs beyond MUSA_SDK
)

# Minimum MUSA SDK version required (empty = any version)
declare -A COMPONENT_MIN_MUSA_VER=(
    ["torch_musa"]="2.5.0"
    ["paddle_musa"]="3.0.0"
    ["tensorflow_musa_extension"]="3.0.0"
)

# classify_component <repo_name>
# Echoes one of: core | ml-framework | application
# Unknown names default to "application".
classify_component() {
    local repo_name="$1"
    case "${repo_name}" in
        # core
        muThrust|muAlg|mutlass)
            echo "core"
            ;;
        # ml-framework
        torch_musa|paddle_musa|tensorflow_musa_extension)
            echo "ml-framework"
            ;;
        # all other known repos and unknown names
        *)
            echo "application"
            ;;
    esac
}

# ── Version comparison helper ─────────────────────────────────────────────────
# version_gte <ver_a> <ver_b>
# Returns 0 (true) if ver_a >= ver_b (semver comparison via sort -V), 1 otherwise.
version_gte() {
    local ver_a="$1"
    local ver_b="$2"
    # sort -V sorts versions; if ver_b comes first (or they're equal), ver_a >= ver_b
    local lowest
    lowest="$(printf '%s\n%s\n' "${ver_a}" "${ver_b}" | sort -V | head -n1)"
    if [[ "${lowest}" == "${ver_b}" ]]; then
        return 0
    else
        return 1
    fi
}

# ── Dependency resolution ─────────────────────────────────────────────────────
# Global array populated by resolve_dependency_order()
RESOLVED_ORDER=()

# resolve_dependency_order [component...]
# Performs a topological sort (Kahn's algorithm) over the given components.
# If no arguments are given, uses all 57 components from COMPONENT_TIERS.
# Skips components whose COMPONENT_MIN_MUSA_VER exceeds SDK_VERSION.
# Populates RESOLVED_ORDER and prints the ordered list to stdout.
resolve_dependency_order() {
    local -a input_components=("$@")

    # Default to all known components if none specified
    if [[ ${#input_components[@]} -eq 0 ]]; then
        input_components=("${!COMPONENT_TIERS[@]}")
    fi

    # Build a set of requested components (excluding MUSA_SDK which is a virtual node)
    declare -A requested=()
    for comp in "${input_components[@]}"; do
        requested["${comp}"]=1
    done

    # Filter out components that don't meet the minimum MUSA SDK version requirement
    declare -A skipped=()
    for comp in "${!requested[@]}"; do
        local min_ver="${COMPONENT_MIN_MUSA_VER[${comp}]:-}"
        if [[ -n "${min_ver}" && -n "${SDK_VERSION}" ]]; then
            if ! version_gte "${SDK_VERSION}" "${min_ver}"; then
                log_warn "Skipping ${comp}: requires MUSA SDK >= ${min_ver}, installed: ${SDK_VERSION}"
                skipped["${comp}"]=1
                unset "requested[${comp}]"
            fi
        fi
    done

    # Build in-degree map and adjacency list for Kahn's algorithm
    # Only consider edges between components in the requested set
    declare -A in_degree=()
    declare -A dependents=()  # dependents[A] = space-separated list of components that depend on A

    for comp in "${!requested[@]}"; do
        in_degree["${comp}"]="${in_degree[${comp}]:-0}"
    done

    for comp in "${!requested[@]}"; do
        local prereqs="${COMPONENT_PREREQS[${comp}]:-}"
        for prereq in ${prereqs}; do
            # Skip virtual MUSA_SDK node and skipped/unknown components
            if [[ "${prereq}" == "MUSA_SDK" ]]; then
                continue
            fi
            if [[ -z "${requested[${prereq}]:-}" ]]; then
                continue
            fi
            # prereq -> comp edge: increment in_degree of comp
            in_degree["${comp}"]=$(( ${in_degree[${comp}]:-0} + 1 ))
            dependents["${prereq}"]="${dependents[${prereq}]:-} ${comp}"
        done
    done

    # Initialise queue with all zero-in-degree nodes (sorted for determinism)
    local -a queue=()
    for comp in $(printf '%s\n' "${!in_degree[@]}" | sort); do
        if [[ "${in_degree[${comp}]}" -eq 0 ]]; then
            queue+=("${comp}")
        fi
    done

    RESOLVED_ORDER=()
    while [[ ${#queue[@]} -gt 0 ]]; do
        # Dequeue front element
        local current="${queue[0]}"
        queue=("${queue[@]:1}")
        RESOLVED_ORDER+=("${current}")

        # Reduce in-degree for each dependent
        local deps="${dependents[${current}]:-}"
        local -a newly_zero=()
        for dep in ${deps}; do
            in_degree["${dep}"]=$(( ${in_degree[${dep}]} - 1 ))
            if [[ "${in_degree[${dep}]}" -eq 0 ]]; then
                newly_zero+=("${dep}")
            fi
        done
        # Add newly-zero nodes in sorted order for determinism
        for dep in $(printf '%s\n' "${newly_zero[@]:-}" | sort); do
            queue+=("${dep}")
        done
    done

    # Check for cycles (should not occur with a valid DAG)
    if [[ ${#RESOLVED_ORDER[@]} -ne ${#requested[@]} ]]; then
        log_warn "Dependency cycle detected; ${#RESOLVED_ORDER[@]} of ${#requested[@]} components resolved"
    fi

    log_info "Dependency order: ${RESOLVED_ORDER[*]}"
    for comp in "${RESOLVED_ORDER[@]}"; do
        echo "${comp}"
    done
}

# ── .deb extraction engine ────────────────────────────────────────────────────
# Extracts the MUSA SDK from a .deb archive using ar + tar (no dpkg required).
# Installs files under ${MUSA_HOME}, stripping the ./usr/local/musa/ prefix.
extract_musa_sdk() {
    # 1. Locate the .deb file — search SCRIPT_DIR and MTT_lib subdir
    #    Priority order: 3.1.0-pc_x86 > 2.5.0-Kylin > newest available
    local deb_file=""
    local search_dirs=("${SCRIPT_DIR}" "${SCRIPT_DIR}/MTT_lib")

    for search_dir in "${search_dirs[@]}"; do
        if [[ -f "${search_dir}/musa_3.1.0-pc_x86.deb" ]]; then
            deb_file="${search_dir}/musa_3.1.0-pc_x86.deb"
            break
        fi
    done

    if [[ -z "${deb_file}" ]]; then
        for search_dir in "${search_dirs[@]}"; do
            if [[ -f "${search_dir}/musa_2.5.0-Kylin_amd64.deb" ]]; then
                deb_file="${search_dir}/musa_2.5.0-Kylin_amd64.deb"
                break
            fi
        done
    fi

    # Fall back to the newest .deb found in any search dir
    if [[ -z "${deb_file}" ]]; then
        for search_dir in "${search_dirs[@]}"; do
            local candidate
            candidate=$(ls -1 "${search_dir}"/musa_*.deb 2>/dev/null | sort -V | tail -n1 || true)
            if [[ -n "${candidate}" ]]; then
                deb_file="${candidate}"
                break
            fi
        done
    fi

    if [[ -z "${deb_file}" ]]; then
        log_error "No MUSA .deb package found in ${SCRIPT_DIR}/ or ${SCRIPT_DIR}/MTT_lib/"
        log_error "Expected: musa_3.1.0-pc_x86.deb or musa_2.5.0-Kylin_amd64.deb"
        exit 1
    fi
    log_info "Using MUSA package: ${deb_file}"

    # Record the .deb path and extract version from filename (e.g. musa_3.1.0-pc_x86.deb → 3.1.0)
    DEB_SOURCE="${deb_file}"
    local deb_basename
    deb_basename="$(basename "${deb_file}")"
    # Strip leading "musa_" and take everything up to the first "-"
    SDK_VERSION="${deb_basename#musa_}"
    SDK_VERSION="${SDK_VERSION%%-*}"

    # In dry-run mode: skip actual extraction; just report what would happen
    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] Would extract ${deb_file} (SDK version ${SDK_VERSION}) into ${MUSA_HOME}"
        return 0
    fi

    # 2. Create a temp directory and register cleanup trap
    local tmp_dir
    tmp_dir="$(mktemp -d)"
    # shellcheck disable=SC2064
    trap "rm -rf '${tmp_dir}'" EXIT

    log_info "Extracting .deb archive into temp dir: ${tmp_dir}"

    # 3. Unpack the .deb archive using ar (run inside temp dir)
    (cd "${tmp_dir}" && ar x "${deb_file}")

    # 4. Identify the data.tar.* member
    local data_tar=""
    for candidate in "${tmp_dir}"/data.tar.*; do
        if [[ -f "${candidate}" ]]; then
            data_tar="${candidate}"
            break
        fi
    done
    if [[ -z "${data_tar}" ]]; then
        log_error "No data.tar.* member found in ${deb_file}"
        exit 1
    fi
    log_info "Found data archive: $(basename "${data_tar}")"

    # 5. Determine tar flags based on compression extension and extract into subdir
    local data_dir="${tmp_dir}/data"
    mkdir -p "${data_dir}"

    local tar_flags=""
    case "${data_tar}" in
        *.tar.gz)  tar_flags="-xzf" ;;
        *.tar.xz)  tar_flags="-xJf" ;;
        *.tar.zst) tar_flags="--use-compress-program=zstd -xf" ;;
        *.tar.bz2) tar_flags="-xjf" ;;
        *)
            log_error "Unrecognised data archive format: $(basename "${data_tar}")"
            exit 1
            ;;
    esac

    log_info "Extracting $(basename "${data_tar}") into ${data_dir}"
    # shellcheck disable=SC2086
    tar ${tar_flags} "${data_tar}" -C "${data_dir}"

    # 6. Strip ./usr/local/musa/ prefix and copy files into ${MUSA_HOME}
    # ./usr/local/musa/bin/mcc → strip 4 components (., usr, local, musa) → bin/mcc
    # We enumerate files from the extracted tree and copy them preserving structure.
    local musa_src="${data_dir}/usr/local/musa"
    if [[ ! -d "${musa_src}" ]]; then
        log_error "Expected path ./usr/local/musa/ not found inside data archive"
        log_error "Archive contents: $(ls "${data_dir}" 2>/dev/null || true)"
        exit 1
    fi

    log_info "Installing MUSA SDK files to ${MUSA_HOME}"

    # Ensure MUSA_HOME exists
    dry_run_cmd mkdir -p "${MUSA_HOME}"

    # Walk the extracted tree and copy each file/directory
    while IFS= read -r -d '' src_path; do
        # Compute relative path from musa_src
        local rel_path="${src_path#"${musa_src}/"}"
        local dst_path="${MUSA_HOME%/}/${rel_path}"

        if [[ -d "${src_path}" ]]; then
            dry_run_cmd mkdir -p "${dst_path}"
        elif [[ -f "${src_path}" ]]; then
            dry_run_cmd mkdir -p "$(dirname "${dst_path}")"
            dry_run_cmd cp -p "${src_path}" "${dst_path}"
        fi
    done < <(find "${musa_src}" -mindepth 1 -print0 | sort -z)

    log_info "MUSA SDK extraction complete"
}

# ── Environment configuration ─────────────────────────────────────────────────
# Writes /etc/profile.d/musa.sh and /etc/ld.so.conf.d/musa.conf, then runs
# ldconfig to update the dynamic linker cache.
configure_environment() {
    local musa_home="${MUSA_HOME%/}"

    log_info "Writing environment configuration"

    # Write /etc/profile.d/musa.sh
    log_info "Writing /etc/profile.d/musa.sh"
    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] tee /etc/profile.d/musa.sh"
    else
        tee /etc/profile.d/musa.sh > /dev/null <<EOF
export MUSA_HOME="${musa_home}"
export PATH="${musa_home}/bin:\${PATH}"
export LD_LIBRARY_PATH="${musa_home}/lib:\${LD_LIBRARY_PATH:-}"
EOF
    fi

    # Write /etc/ld.so.conf.d/musa.conf
    log_info "Writing /etc/ld.so.conf.d/musa.conf"
    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] tee /etc/ld.so.conf.d/musa.conf"
    else
        tee /etc/ld.so.conf.d/musa.conf > /dev/null <<EOF
${musa_home}/lib
EOF
    fi

    # Update dynamic linker cache
    # Fix any non-symlink versioned .so files that ldconfig expects as symlinks
    log_info "Fixing library symlinks in ${musa_home}/lib"
    if [[ "${DRY_RUN}" != true ]]; then
        while IFS= read -r -d '' sofile; do
            local base dir soname
            base="$(basename "${sofile}")"
            dir="$(dirname "${sofile}")"
            # e.g. libmusa.so.1.5 → soname = libmusa.so.1.5, link target = libmusa.so.1.5.4
            # We only need to ensure the .so → versioned and .so.MAJOR → versioned symlinks exist
            # Find the highest versioned file for this base name
            local stem="${base%.*}"   # libmusa.so.1 from libmusa.so.1.5
            local full_ver
            full_ver="$(ls -1 "${dir}/${stem}".* 2>/dev/null | sort -V | tail -n1 || true)"
            if [[ -n "${full_ver}" && ! -L "${sofile}" && "${sofile}" != "${full_ver}" ]]; then
                ln -sf "$(basename "${full_ver}")" "${sofile}"
                log_info "Fixed symlink: ${base} -> $(basename "${full_ver}")"
            fi
        done < <(find "${musa_home}/lib" -maxdepth 1 -name "*.so.*" ! -type l -print0 2>/dev/null)
    fi

    log_info "Running ldconfig"
    dry_run_cmd ldconfig

    log_info "Environment configuration complete"
}

# ── Installation verification and state recording ─────────────────────────────
# Checks that mcc is present and writes .musa-install-state.json.
verify_and_record_installation() {
    local musa_home="${MUSA_HOME%/}"
    local mcc_path="${musa_home}/bin/mcc"
    local musainfo_path="${musa_home}/bin/musaInfo"

    # 1. Verify mcc exists and is executable (optional — runtime-only packages omit mcc)
    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] Would verify musaInfo at ${musa_home}/bin/musaInfo"
    elif [[ ! -x "${musa_home}/bin/musaInfo" ]]; then
        log_error "musaInfo not found at ${musa_home}/bin/musaInfo — SDK extraction may have failed"
        exit 1
    else
        log_info "musaInfo found at ${musa_home}/bin/musaInfo"
        if [[ -x "${mcc_path}" ]]; then
            log_info "mcc compiler found at ${mcc_path}"
        else
            log_warn "mcc compiler not present (runtime-only SDK) — smoke tests will be skipped"
        fi
    fi

    # 2. Build the JSON content
    local installed_at
    installed_at="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

    local state_file="${musa_home}/.musa-install-state.json"
    local json_content
    json_content="$(cat <<EOF
{
  "musa_home": "${musa_home}",
  "sdk_version": "${SDK_VERSION}",
  "deb_source": "${DEB_SOURCE}",
  "mcc_path": "${mcc_path}",
  "lib_path": "${musa_home}/lib",
  "installed_at": "${installed_at}"
}
EOF
)"

    # 3. Write the state file (or dry-run)
    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] Would write ${state_file}:"
        echo "${json_content}"
    else
        printf '%s\n' "${json_content}" > "${state_file}"
    fi

    log_info "Installation verified: musaInfo found at ${musainfo_path}"
    log_info "Install state written to ${state_file}"
}

# ── Compile-time smoke test ───────────────────────────────────────────────────
# smoke_test_component <component_name> <header_include>
# Creates a minimal .mu source file that includes <header_include>, compiles it
# with mcc, and reports PASS or FAIL.  Never exits on failure — only warns.
# In dry-run mode prints a [DRY-RUN] message and returns 0.
smoke_test_component() {
    local comp_name="$1"
    local header_include="$2"

    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] Would smoke-test ${comp_name}"
        return 0
    fi

    local tmp_dir
    tmp_dir="$(mktemp -d)"
    local src_file="${tmp_dir}/smoke_${comp_name}.mu"

    # Write the minimal source file
    cat > "${src_file}" <<EOF
#include "${header_include}"
int main() { return 0; }
EOF

    # Attempt compilation; discard output binary to /dev/null
    if "${MUSA_HOME%/}/bin/mcc" \
            -I"${MUSA_HOME%/}/include" \
            -L"${MUSA_HOME%/}/lib" \
            -o /dev/null \
            "${src_file}" \
            > /dev/null 2>&1; then
        log_info "[PASS] ${comp_name} smoke test"
    else
        log_warn "[FAIL] ${comp_name} smoke test"
    fi

    rm -rf "${tmp_dir}"
}

# ── Core MTT_lib build sequence ───────────────────────────────────────────────
# muThrust, muAlg, mutlass are header-only libraries.
# When mcc is available: build via CMake/mt_build.sh.
# When runtime-only SDK: copy headers directly into MUSA_HOME/include/.
build_core_components() {
    local build_log="/var/log/mtt-s30-build.log"
    local musa_inc="${MUSA_HOME%/}/include"

    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] Would install muThrust/muAlg/mutlass headers to ${musa_inc}"
        return 0
    fi

    mkdir -p "${musa_inc}"

    # ── muThrust (header-only) ────────────────────────────────────────────────
    local thrust_src="${SCRIPT_DIR}/MTT_lib/muThrust"
    if [[ -d "${thrust_src}/thrust" ]]; then
        if [[ -x "${MUSA_HOME%/}/bin/mcc" ]]; then
            log_info "Building muThrust via mt_build.sh..."
            (cd "${thrust_src}" && bash mt_build.sh -i -d "${MUSA_HOME%/}" >> "${build_log}" 2>&1) || \
                log_warn "muThrust build failed — falling back to header copy"
        fi
        # Always ensure headers are present (build may have installed them, or we copy directly)
        if [[ ! -d "${musa_inc}/thrust" ]]; then
            cp -r "${thrust_src}/thrust" "${musa_inc}/"
            log_info "muThrust headers installed to ${musa_inc}/thrust/"
        else
            log_info "muThrust headers already present at ${musa_inc}/thrust/"
        fi
    else
        log_warn "muThrust source not found at ${thrust_src}"
    fi

    # ── muAlg (header-only, CUB-based) ───────────────────────────────────────
    local mualg_src="${SCRIPT_DIR}/MTT_lib/muAlg"
    if [[ -d "${mualg_src}/cub" ]]; then
        if [[ -x "${MUSA_HOME%/}/bin/mcc" ]]; then
            log_info "Building muAlg via mt_build.sh..."
            (cd "${mualg_src}" && bash mt_build.sh -i -d "${MUSA_HOME%/}" >> "${build_log}" 2>&1) || \
                log_warn "muAlg build failed — falling back to header copy"
        fi
        if [[ ! -d "${musa_inc}/cub" ]]; then
            cp -r "${mualg_src}/cub" "${musa_inc}/"
            log_info "muAlg (CUB) headers installed to ${musa_inc}/cub/"
        else
            log_info "muAlg headers already present at ${musa_inc}/cub/"
        fi
    else
        log_warn "muAlg source not found at ${mualg_src}"
    fi

    # ── mutlass (header-only) ─────────────────────────────────────────────────
    local mutlass_src="${SCRIPT_DIR}/MTT_lib/mutlass"
    if [[ -d "${mutlass_src}/include" ]]; then
        for hdr_dir in mutlass mute; do
            if [[ -d "${mutlass_src}/include/${hdr_dir}" ]]; then
                if [[ ! -d "${musa_inc}/${hdr_dir}" ]]; then
                    cp -r "${mutlass_src}/include/${hdr_dir}" "${musa_inc}/"
                    log_info "mutlass ${hdr_dir} headers installed to ${musa_inc}/${hdr_dir}/"
                else
                    log_info "mutlass ${hdr_dir} headers already present"
                fi
            fi
        done
    else
        log_warn "mutlass source not found at ${mutlass_src}"
    fi

    log_info "Core MTT_lib header libraries installed"
}

    # Helper: build a single component from ${SCRIPT_DIR}/MTT_lib/<name>/
    # Usage: build_component <component_name>
    build_component() {
        local comp="$1"
        local comp_dir="${SCRIPT_DIR}/MTT_lib/${comp}"

        if [[ "${DRY_RUN}" == true ]]; then
            echo "[DRY-RUN] Would build ${comp}"
            return 0
        fi

        export MUSA_HOME="${MUSA_HOME}"
        export LD_LIBRARY_PATH="${MUSA_HOME%/}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

        (
            cd "${comp_dir}"
            if [[ -f "CMakeLists.txt" ]]; then
                cmake -B build \
                    -DCMAKE_INSTALL_PREFIX="${MUSA_HOME}" \
                    -DMUSA_HOME="${MUSA_HOME}" \
                    >> "${build_log}" 2>&1
                cmake --build build -j"$(nproc)" >> "${build_log}" 2>&1
                cmake --install build >> "${build_log}" 2>&1
            else
                make install \
                    PREFIX="${MUSA_HOME}" \
                    MUSA_HOME="${MUSA_HOME}" \
                    -j"$(nproc)" \
                    >> "${build_log}" 2>&1
            fi
        )

        local exit_code=$?
        if [[ ${exit_code} -ne 0 ]]; then
            log_error "Build failed for component: ${comp}"
            echo "--- Last 20 lines of build log (${build_log}) ---" >&2
            tail -n 20 "${build_log}" >&2
            exit 1
        fi
    }

    # Initialise (or truncate) the build log
    if [[ "${DRY_RUN}" != true ]]; then
        : > "${build_log}"
    fi
    log_info "Build log: ${build_log}"

    # ── muThrust ──────────────────────────────────────────────────────────────
    log_info "Building muThrust..."
    if [[ "${DRY_RUN}" == true ]]; then
        dry_run_cmd build_component muThrust
    else
        build_component muThrust
        dry_run_cmd mkdir -p "${MUSA_HOME%/}/include"
        log_info "muThrust headers installed to ${MUSA_HOME%/}/include/"
    fi
    smoke_test_component "muThrust" "thrust/thrust.h"

    # ── muAlg ─────────────────────────────────────────────────────────────────
    log_info "Building muAlg..."
    if [[ "${DRY_RUN}" == true ]]; then
        dry_run_cmd build_component muAlg
    else
        build_component muAlg
        dry_run_cmd mkdir -p "${MUSA_HOME%/}/lib"
        log_info "muAlg library installed to ${MUSA_HOME%/}/lib/"
    fi
    smoke_test_component "muAlg" "mualg/mualg.h"

    # ── mutlass ───────────────────────────────────────────────────────────────
    log_info "Building mutlass..."
    if [[ "${DRY_RUN}" == true ]]; then
        dry_run_cmd build_component mutlass
    else
        build_component mutlass
        dry_run_cmd mkdir -p "${MUSA_HOME%/}/include/mutlass"
        log_info "mutlass headers installed to ${MUSA_HOME%/}/include/mutlass/"
    fi
    smoke_test_component "mutlass" "mutlass/mutlass.h"

    log_info "Core MTT_lib components built successfully"
}

# ── torch_musa build ─────────────────────────────────────────────────────────
# Builds and installs torch_musa from Driver/MTT_lib/torch_musa/.
# USE_MCCL is hard-coded to 0: the S30 (device IDs 0x0102/0x0103) does not
# support MCCL.
build_torch_musa() {
    local build_log="/var/log/mtt-s30-build.log"

    # 1. Skip flag
    if [[ "${SKIP_TORCH}" == true ]]; then
        log_info "Skipping torch_musa build (--skip-torch)"
        return 0
    fi

    # 2. Dry-run
    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] Would build torch_musa"
        return 0
    fi

    # 3. Set required environment variables
    export MUSA_HOME="${MUSA_HOME}"
    log_info "Setting MUSA_HOME=${MUSA_HOME}"

    export LD_LIBRARY_PATH="${MUSA_HOME%/}/lib:${LD_LIBRARY_PATH:-}"
    log_info "Setting LD_LIBRARY_PATH=${LD_LIBRARY_PATH}"

    if [[ -z "${PYTORCH_REPO_PATH:-}" ]]; then
        PYTORCH_REPO_PATH="${SCRIPT_DIR}/MTT_lib/torch_musa/pytorch"
    fi
    export PYTORCH_REPO_PATH
    log_info "Setting PYTORCH_REPO_PATH=${PYTORCH_REPO_PATH}"

    export USE_MCCL=0
    log_info "Setting USE_MCCL=${USE_MCCL} (S30 does not support MCCL)"

    # 4. Run the build script
    local torch_musa_dir="${SCRIPT_DIR}/MTT_lib/torch_musa"
    log_info "Building torch_musa in ${torch_musa_dir}..."
    (
        cd "${torch_musa_dir}"
        bash build.sh -m -w >> "${build_log}" 2>&1
    )

    local exit_code=$?
    if [[ ${exit_code} -ne 0 ]]; then
        log_error "Build failed for component: torch_musa"
        echo "--- Last 20 lines of build log (${build_log}) ---" >&2
        tail -n 20 "${build_log}" >&2
        exit 1
    fi

    # 5. Find the generated wheel
    local wheel_file
    wheel_file="$(find "${torch_musa_dir}/dist/" -maxdepth 1 -name "*.whl" | head -n1)"
    if [[ -z "${wheel_file}" ]]; then
        log_error "No .whl file found in ${torch_musa_dir}/dist/"
        exit 1
    fi
    log_info "Found wheel: ${wheel_file}"

    # 6. Install the wheel
    log_info "Installing ${wheel_file}..."
    pip install --force-reinstall "${wheel_file}"

    # 7. Verify the installation (warn only on failure)
    if python3 -c "import torch_musa; print(torch_musa.__version__)"; then
        log_info "torch_musa installed successfully"
    else
        log_warn "torch_musa import verification failed — the package may not be fully functional"
    fi
}

# ── Application-tier component builds ────────────────────────────────────────
# Iterates over all application-tier components in RESOLVED_ORDER and attempts
# to build each one.  Failures are warnings only — the pipeline never aborts.
build_application_components() {
    local build_log="/var/log/mtt-s30-build.log"

    # Skip if mcc compiler is not available (runtime-only SDK)
    if [[ "${DRY_RUN}" != true ]] && [[ ! -x "${MUSA_HOME%/}/bin/mcc" ]]; then
        log_warn "mcc compiler not found — skipping application component builds (runtime-only SDK)"
        return 0
    fi

    for comp in "${RESOLVED_ORDER[@]}"; do
        if [[ "$(classify_component "${comp}")" != "application" ]]; then
            continue
        fi

        log_info "Building application component: ${comp}"

        if [[ "${DRY_RUN}" == true ]]; then
            echo "[DRY-RUN] Would build ${comp}"
            continue
        fi

        local comp_dir="${SCRIPT_DIR}/MTT_lib/${comp}"

        export MUSA_HOME="${MUSA_HOME}"
        export LD_LIBRARY_PATH="${MUSA_HOME%/}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

        (
            cd "${comp_dir}"
            if [[ -f "CMakeLists.txt" ]]; then
                cmake -B build \
                    -DCMAKE_INSTALL_PREFIX="${MUSA_HOME}" \
                    -DMUSA_HOME="${MUSA_HOME}" \
                    >> "${build_log}" 2>&1
                cmake --build build -j"$(nproc)" >> "${build_log}" 2>&1
                cmake --install build >> "${build_log}" 2>&1
            else
                make install \
                    PREFIX="${MUSA_HOME}" \
                    MUSA_HOME="${MUSA_HOME}" \
                    -j"$(nproc)" \
                    >> "${build_log}" 2>&1
            fi
        ) || {
            log_warn "Optional component ${comp} failed to build — skipping"
            continue
        }
    done
}

# ── Python packages from MTT_lib ──────────────────────────────────────────────
# Installs Python packages that don't require GPU compute or mcc compiler.
# These work with the runtime-only SDK.
install_python_packages() {
    if [[ "${DRY_RUN}" == true ]]; then
        echo "[DRY-RUN] Would install Python packages: MT-DualPipe, mthreads-ml-py"
        return 0
    fi

    local pip_cmd
    pip_cmd="$(command -v pip3 || command -v pip || echo "")"
    if [[ -z "${pip_cmd}" ]]; then
        log_warn "pip not found — skipping Python package installation"
        return 0
    fi

    # MT-DualPipe: pure Python pipeline parallelism, only needs PyTorch
    local dualpipe_dir="${SCRIPT_DIR}/MTT_lib/MT-DualPipe"
    if [[ -d "${dualpipe_dir}" ]] && [[ -f "${dualpipe_dir}/setup.py" || -f "${dualpipe_dir}/pyproject.toml" ]]; then
        log_info "Installing MT-DualPipe..."
        "${pip_cmd}" install "${dualpipe_dir}" --quiet 2>&1 | tail -3 || \
            log_warn "MT-DualPipe installation failed"
        python3 -c "import dualpipe; print('  dualpipe OK:', dualpipe.__file__)" 2>/dev/null || \
            log_warn "  dualpipe import check failed"
    fi

    # mthreads-ml-py: MTML Python bindings, available on PyPI
    if ! python3 -c "import pymtml" 2>/dev/null; then
        log_info "Installing mthreads-ml-py from PyPI..."
        "${pip_cmd}" install mthreads-ml-py --quiet 2>&1 | tail -3 || \
            log_warn "mthreads-ml-py installation failed"
    else
        log_info "mthreads-ml-py already installed"
    fi

    # mtt-monitor: our GPU monitoring tool
    if [[ -f "${SCRIPT_DIR}/tools/mtt-monitor" ]]; then
        cp "${SCRIPT_DIR}/tools/mtt-monitor" /usr/local/bin/mtt-monitor
        chmod +x /usr/local/bin/mtt-monitor
        log_info "mtt-monitor installed to /usr/local/bin/"
    fi

    log_info "Python package installation complete"
}

# ── Main ──────────────────────────────────────────────────────────────────────
main() {
    log_info "MTT S30 MUSA SDK Setup"
    log_info "  MUSA_HOME   : ${MUSA_HOME}"
    log_info "  SKIP_TORCH  : ${SKIP_TORCH}"
    log_info "  DRY_RUN     : ${DRY_RUN}"
    log_info "  SCRIPT_DIR  : ${SCRIPT_DIR}"

    check_existing_installation
    extract_musa_sdk
    configure_environment
    verify_and_record_installation

    # Resolve dependency order for all components (core, ml-framework, application)
    resolve_dependency_order

    build_core_components
    build_torch_musa
    build_application_components
    install_python_packages
}

main "$@"
