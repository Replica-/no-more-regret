#!/usr/bin/env bash
# The window's shaders (port/system/shaders/*.hlsl, shading.h) made into
# what the program holds of them: each NAME.hlsl's fragment shader as DXIL
# (Direct3D 12) and SPIR-V (Vulkan), the bytes of each in NAME.inc
# (NAME_dxil and NAME_spirv, with their sizes), which the build takes as
# they are: it needs no compiler of shaders. Run again after changing a
# shader. DXC is the compiler: the Windows SDK's dxc.exe by default, which
# signs the DXIL with the dxil.dll beside it but makes no SPIR-V (the
# release of DirectXShaderCompiler's on GitHub does); without, NAME_spirv
# is empty and the window shades with Direct3D 12 only.
set -euo pipefail

here=$(cd "$(dirname "$0")/../system/shaders" && pwd)
dxc=${DXC:-"/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

native() {
    if command -v cygpath >/dev/null; then cygpath -m "$1"; else echo "$1"; fi
}

# The bytes of file as name and name_size, or none.
bytes() {
    if [ -s "$2" ]; then
        echo "const unsigned char $1[] = {"
        xxd -i < "$2"
        echo "};"
        echo "const std::size_t $1_size = sizeof $1;"
    else
        echo "const unsigned char *const $1 = nullptr;"
        echo "const std::size_t $1_size = 0;"
    fi
}

for source in "$here"/*.hlsl; do
    name=$(basename "$source" .hlsl)
    "$dxc" -nologo -T ps_6_0 -E main -O3 -Wno-ignored-attributes \
        -Fo "$(native "$work/$name.dxil")" "$(native "$source")"
    if ! "$dxc" -nologo -T ps_6_0 -E main -O3 -spirv -fspv-target-env=vulkan1.0 \
        -Fo "$(native "$work/$name.spv")" "$(native "$source")" 2>"$work/spirv.log"; then
        echo "$name: no SPIR-V ($(tail -n 1 "$work/spirv.log"))" >&2
        rm -f "$work/$name.spv"
    fi
    {
        echo "/* Made by port/tools/shaders.sh of $name.hlsl: not to be edited. */"
        bytes "${name}_dxil" "$work/$name.dxil"
        bytes "${name}_spirv" "$work/$name.spv"
    } > "$here/$name.inc"
    echo "$name.inc"
done
