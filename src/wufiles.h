// SPDX-License-Identifier: MIT
// wufiles: the file formats around a work unit that minicore reads and writes. bzip2 and gzip in memory, a
// ustar reader and writer, core.xml as a flat key/value map, and atomic writes with a length and hash
// footer. File IO errors throw FileError, bad data throws DataError.
#ifndef MINICORE_WUFILES_H
#define MINICORE_WUFILES_H
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct FileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct DataError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

bool fileExists(const std::string& path);
std::string readFile(const std::string& path);
long long fileSize(const std::string& path);  // -1 if missing

bool isBzip2(const std::string& data);
std::string bunzip2(const std::string& packed, const std::string& name);
std::string bzip2(const std::string& data);
std::string gunzip(const std::string& packed, const std::string& name);

// The 512-byte header a FAH work server puts before the tar in wudata_01.dat (seen on project 18264,
// core 0x27): big-endian u32 fields at fixed offsets, two 20-byte digests and a 200-byte signature region.
// The digest and signature scheme is FAH's and private; this reads the plain fields and nothing more.
struct WuHeader {
    uint32_t version = 0, coreType = 0, project = 0, run = 0, clone = 0, gen = 0, payloadBytes = 0;
};
// The header, if the first block is one: no tar or bzip2 data at 0, and a tar or bzip2 stream 512 bytes on.
std::optional<WuHeader> readWuHeader(const std::string& data);

// Regular members of a ustar or GNU tar archive, in order. Other member types (directories, pax
// headers) are skipped. A header whose checksum is wrong throws.
std::vector<std::pair<std::string, std::string>> readTar(const std::string& archive);
std::string writeTar(const std::vector<std::pair<std::string, std::string>>& members);

// core.xml: <config><key>value</key>...</config>, or <key v="value"/> as in the client's own config.
// Keys keep their case; values are trimmed.
std::map<std::string, std::string> parseCoreXml(const std::string& xml);

// 64-bit FNV-1a, the hash in the footer below and in a checkpoint's unit id.
uint64_t fnv1a(const void* data, size_t size);
// Writes data, then its length and FNV-1a hash, to path.tmp, syncs it and renames it over path.
void writeAtomically(const std::string& path, const std::string& data);
// The body if the footer matches; empty if the file is missing, truncated or corrupt.
std::string readVerified(const std::string& path);
// A plain atomic write (no footer).
void replaceFile(const std::string& path, const std::string& data);
#endif
