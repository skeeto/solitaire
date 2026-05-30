# Stamp a content hash into the service worker's cache name so each build (and
# thus each deploy) uses a fresh cache and supersedes the previous one.
# Invoked at build time: cmake -DSW_IN= -DWASM= -DOUT= -P stamp_sw.cmake
file(SHA256 "${WASM}" HASH)
string(SUBSTRING "${HASH}" 0 12 SHORT)
file(READ "${SW_IN}" CONTENT)
string(REPLACE "@CACHE_VERSION@" "${SHORT}" CONTENT "${CONTENT}")
file(WRITE "${OUT}" "${CONTENT}")
