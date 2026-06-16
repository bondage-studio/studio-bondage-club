# DownloadCEF.cmake — acquire the CEF (Chromium Embedded Framework) "minimal"
# binary distribution from the official cef-builds CDN, verify it against the
# published SHA1, extract it, and expose CEF_ROOT for find_package(CEF).
#
# Knobs (cache variables, all optional):
#   CEF_ROOT      Path to a pre-extracted distribution. When set, nothing is
#                 downloaded — the build consumes the SDK as-is.
#   CEF_VERSION   Pin an exact build string, e.g.
#                 "120.1.10+g3ce3184+chromium-120.0.6099.129". When empty, the
#                 latest "stable" build for the host arch is resolved from the
#                 CDN index.json.
#   CEF_PLATFORM  cef-builds platform slug; auto-detected from the host arch.
#
# The distribution is cached under desktop/third_party/cef so it is fetched once
# and shared across build directories (and is git-ignored).

set(CEF_ROOT "" CACHE PATH "Path to a pre-extracted CEF distribution (skips download)")
set(CEF_VERSION "" CACHE STRING "Pin a CEF build string (empty = latest stable)")
set(CEF_PLATFORM "" CACHE STRING "CEF platform slug (empty = auto-detect)")

if(CEF_ROOT)
  message(STATUS "CEF: using pre-extracted distribution at ${CEF_ROOT}")
  return()
endif()

# --- Host platform slug -----------------------------------------------------
if(NOT CEF_PLATFORM)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CEF_PLATFORM "linuxarm64")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CEF_PLATFORM "linux64")
  else()
    set(CEF_PLATFORM "linux32")
  endif()
endif()

set(_cef_cdn "https://cef-builds.spotifycdn.com")
set(_cef_dl_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef")
file(MAKE_DIRECTORY "${_cef_dl_dir}")

# --- Resolve the distribution file name + SHA1 ------------------------------
set(_cef_file "")
set(_cef_sha1 "")

if(CEF_VERSION)
  set(_cef_file "cef_binary_${CEF_VERSION}_${CEF_PLATFORM}_minimal.tar.bz2")
else()
  message(STATUS "CEF: resolving latest stable build for ${CEF_PLATFORM} from ${_cef_cdn}/index.json")
  set(_cef_index "${_cef_dl_dir}/index.json")
  file(DOWNLOAD "${_cef_cdn}/index.json" "${_cef_index}" STATUS _idx_status TLS_VERIFY ON)
  list(GET _idx_status 0 _idx_code)
  if(NOT _idx_code EQUAL 0)
    list(GET _idx_status 1 _idx_msg)
    message(FATAL_ERROR
      "CEF: failed to download index.json (${_idx_msg}). Pin a build with "
      "-DCEF_VERSION=<build> or point at a local SDK with -DCEF_ROOT=<dir>.")
  endif()
  file(READ "${_cef_index}" _idx)

  string(JSON _vers_count ERROR_VARIABLE _json_err LENGTH "${_idx}" "${CEF_PLATFORM}" versions)
  if(_json_err)
    message(FATAL_ERROR "CEF: index.json has no versions for platform '${CEF_PLATFORM}': ${_json_err}")
  endif()
  math(EXPR _vlast "${_vers_count} - 1")

  # Versions are newest-first; take the first one on the "stable" channel and
  # its "minimal"-type file.
  foreach(_i RANGE ${_vlast})
    string(JSON _channel GET "${_idx}" "${CEF_PLATFORM}" versions ${_i} channel)
    if(NOT _channel STREQUAL "stable")
      continue()
    endif()
    string(JSON _resolved_ver GET "${_idx}" "${CEF_PLATFORM}" versions ${_i} cef_version)
    string(JSON _files_count LENGTH "${_idx}" "${CEF_PLATFORM}" versions ${_i} files)
    if(_files_count GREATER 0)
      math(EXPR _flast "${_files_count} - 1")
      foreach(_j RANGE ${_flast})
        string(JSON _ftype GET "${_idx}" "${CEF_PLATFORM}" versions ${_i} files ${_j} type)
        if(_ftype STREQUAL "minimal")
          string(JSON _cef_file GET "${_idx}" "${CEF_PLATFORM}" versions ${_i} files ${_j} name)
          string(JSON _cef_sha1 GET "${_idx}" "${CEF_PLATFORM}" versions ${_i} files ${_j} sha1)
          break()
        endif()
      endforeach()
    endif()
    if(_cef_file)
      message(STATUS "CEF: resolved stable build ${_resolved_ver}")
    endif()
    break()
  endforeach()

  if(NOT _cef_file)
    message(FATAL_ERROR "CEF: no stable 'minimal' build for ${CEF_PLATFORM} found in index.json")
  endif()
endif()

# Extracted directory name == file name minus the .tar.bz2 suffix.
string(REGEX REPLACE "\\.tar\\.bz2$" "" _cef_dist "${_cef_file}")
set(_cef_root_dir "${_cef_dl_dir}/${_cef_dist}")

if(IS_DIRECTORY "${_cef_root_dir}")
  message(STATUS "CEF: using cached distribution ${_cef_dist}")
  set(CEF_ROOT "${_cef_root_dir}" CACHE PATH "Path to a pre-extracted CEF distribution" FORCE)
  return()
endif()

# The build string contains '+' which must be percent-encoded in the URL.
set(_cef_url "${_cef_cdn}/${_cef_file}")
string(REPLACE "+" "%2B" _cef_url "${_cef_url}")
set(_cef_archive "${_cef_dl_dir}/${_cef_file}")

# Pinned-version path has no SHA1 yet; fetch the companion .sha1 the CDN serves.
if(NOT _cef_sha1)
  file(DOWNLOAD "${_cef_url}.sha1" "${_cef_archive}.sha1" STATUS _sha_status TLS_VERIFY ON)
  list(GET _sha_status 0 _sha_code)
  if(NOT _sha_code EQUAL 0)
    list(GET _sha_status 1 _sha_msg)
    message(FATAL_ERROR "CEF: failed to download SHA1 for ${_cef_file} (${_sha_msg})")
  endif()
  file(READ "${_cef_archive}.sha1" _cef_sha1)
  string(STRIP "${_cef_sha1}" _cef_sha1)
endif()

message(STATUS "CEF: downloading ${_cef_file} (~150 MB, one-time) ...")
file(DOWNLOAD "${_cef_url}" "${_cef_archive}"
  EXPECTED_HASH SHA1=${_cef_sha1}
  SHOW_PROGRESS TLS_VERIFY ON STATUS _dl_status)
list(GET _dl_status 0 _dl_code)
if(NOT _dl_code EQUAL 0)
  list(GET _dl_status 1 _dl_msg)
  file(REMOVE "${_cef_archive}")
  message(FATAL_ERROR "CEF: download failed (${_dl_msg})")
endif()

message(STATUS "CEF: extracting ${_cef_file} ...")
# Omit the compression letter: cmake -E tar auto-detects bzip2 on extract.
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E tar xf "${_cef_archive}"
  WORKING_DIRECTORY "${_cef_dl_dir}"
  RESULT_VARIABLE _tar_rc)
if(NOT _tar_rc EQUAL 0)
  message(FATAL_ERROR "CEF: extraction of ${_cef_file} failed (code ${_tar_rc})")
endif()
file(REMOVE "${_cef_archive}" "${_cef_archive}.sha1")

if(NOT IS_DIRECTORY "${_cef_root_dir}")
  message(FATAL_ERROR "CEF: expected extracted directory not found: ${_cef_root_dir}")
endif()

set(CEF_ROOT "${_cef_root_dir}" CACHE PATH "Path to a pre-extracted CEF distribution" FORCE)
message(STATUS "CEF: ready at ${CEF_ROOT}")
