#!/usr/bin/env python3
"""Generate a C++ translation unit embedding web/dist as a virtual filesystem.

Usage: embed_assets.py <dist_dir> <output_cpp>

The generated file implements sbc::server::make_embedded_asset_source(), backed
by an in-memory map of dist-relative paths to file bytes.
"""
import os
import sys


def main() -> int:
    if len(sys.argv) != 3:
        sys.stderr.write("usage: embed_assets.py <dist_dir> <output_cpp>\n")
        return 2
    dist_dir, out_path = sys.argv[1], sys.argv[2]

    files = []
    for root, _dirs, names in os.walk(dist_dir):
        for name in names:
            full = os.path.join(root, name)
            rel = os.path.relpath(full, dist_dir).replace(os.sep, "/")
            files.append((rel, full))
    files.sort()

    out = []
    out.append('#include "server/embedded_assets.hpp"')
    out.append("")
    out.append("#include <memory>")
    out.append("#include <optional>")
    out.append("#include <string>")
    out.append("#include <unordered_map>")
    out.append("")
    out.append("namespace sbc::server {")
    out.append("namespace {")

    for i, (_rel, full) in enumerate(files):
        with open(full, "rb") as fh:
            data = fh.read()
        out.append(f"const unsigned char kAsset{i}[] = {{")
        line = []
        for j, byte in enumerate(data):
            line.append(str(byte))
            if len(line) == 32:
                out.append("    " + ",".join(line) + ",")
                line = []
        if line:
            out.append("    " + ",".join(line) + ",")
        out.append("};")

    out.append("")
    out.append("const std::unordered_map<std::string, std::string>& table() {")
    out.append("    static const std::unordered_map<std::string, std::string> t = {")
    for i, (rel, _full) in enumerate(files):
        esc = rel.replace("\\", "\\\\").replace('"', '\\"')
        out.append(
            f'        {{"{esc}", std::string(reinterpret_cast<const char*>(kAsset{i}), '
            f"sizeof(kAsset{i}))}},"
        )
    out.append("    };")
    out.append("    return t;")
    out.append("}")
    out.append("")
    out.append("class EmbeddedAssetSource : public AssetSource {")
    out.append("public:")
    out.append("    bool available() const override { return !table().empty(); }")
    out.append("    std::optional<std::string> read(const std::string& rel_path) override {")
    out.append("        auto it = table().find(rel_path);")
    out.append("        if (it == table().end()) return std::nullopt;")
    out.append("        return it->second;")
    out.append("    }")
    out.append("};")
    out.append("")
    out.append("}  // namespace")
    out.append("")
    out.append("std::shared_ptr<AssetSource> make_embedded_asset_source() {")
    out.append("    return std::make_shared<EmbeddedAssetSource>();")
    out.append("}")
    out.append("")
    out.append("}  // namespace sbc::server")
    out.append("")

    with open(out_path, "w") as fh:
        fh.write("\n".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
