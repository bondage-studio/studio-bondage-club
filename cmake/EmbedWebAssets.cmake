# embed_web_assets(<target> <dist_dir>)
#
# Generates a C++ translation unit that compiles the contents of <dist_dir>
# (the built web/dist) into <target>, exposed via make_embedded_asset_source().
# Regenerates whenever any asset file changes.
function(embed_web_assets target dist_dir)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    get_filename_component(_sbc_repo_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/embedded_assets_generated.cpp")
    file(GLOB_RECURSE asset_files "${dist_dir}/*")

    add_custom_command(
        OUTPUT "${generated}"
        COMMAND ${Python3_EXECUTABLE}
                "${_sbc_repo_root}/tools/embed_assets.py"
                "${dist_dir}" "${generated}"
        DEPENDS "${_sbc_repo_root}/tools/embed_assets.py" ${asset_files}
        COMMENT "Embedding web/dist assets into the binary"
        VERBATIM)

    target_sources(${target} PRIVATE "${generated}")
endfunction()
