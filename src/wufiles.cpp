// SPDX-License-Identifier: MIT
#include "wufiles.h"
#include <bzlib.h>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

bool fileExists(const string& path) {
    struct stat s;
    return stat(path.c_str(), &s) == 0;
}

long long fileSize(const string& path) {
    struct stat s;
    return stat(path.c_str(), &s) == 0 ? (long long) s.st_size : -1;
}

string readFile(const string& path) {
    ifstream in(path, ios::binary | ios::ate);
    if (!in)
        throw FileError("cannot read "+path);
    string data((size_t) in.tellg(), '\0');
    in.seekg(0);
    in.read(&data[0], data.size());
    if (!in)
        throw FileError("cannot read "+path);
    return data;
}

bool isBzip2(const string& data) {
    return data.size() >= 4 && data.compare(0, 3, "BZh") == 0 && data[3] >= '1' && data[3] <= '9';
}

// Every stream of packed: pbzip2 and lbzip2 write one per block, and bzip2 -d reads them all.
string bunzip2(const string& packed, const string& name) {
    if (packed.size() > UINT_MAX)
        throw runtime_error(name+" is over 4 GiB, more than libbz2 takes in one call");
    string result;
    vector<char> buffer(1 << 20);
    for (size_t offset = 0; offset < packed.size(); ) {
        bz_stream stream = {};
        if (BZ2_bzDecompressInit(&stream, 0, 0) != BZ_OK)
            throw runtime_error("bzip2 init failed");
        stream.next_in = const_cast<char*>(packed.data())+offset;
        stream.avail_in = (unsigned int) (packed.size()-offset);
        int status;
        do {
            stream.next_out = buffer.data();
            stream.avail_out = (unsigned int) buffer.size();
            status = BZ2_bzDecompress(&stream);
            if (status == BZ_OK || status == BZ_STREAM_END)
                result.append(buffer.data(), buffer.size()-stream.avail_out);
        } while (status == BZ_OK && (stream.avail_in > 0 || stream.avail_out == 0));
        size_t remaining = stream.avail_in;
        BZ2_bzDecompressEnd(&stream);
        if (status == BZ_DATA_ERROR_MAGIC && offset > 0)
            break;  // bytes after the last stream, which bzip2 -d ignores too
        if (status == BZ_OK)
            throw runtime_error("truncated bzip2 data in "+name);
        if (status != BZ_STREAM_END)
            throw runtime_error("corrupt bzip2 data in "+name);
        offset = packed.size()-remaining;
    }
    return result;
}

string bzip2(const string& data) {
    if (data.size() > UINT_MAX/2)
        throw runtime_error("bzip2 input over 2 GiB");
    unsigned int size = (unsigned int) (data.size()+data.size()/100+600);
    string out(size, '\0');
    if (BZ2_bzBuffToBuffCompress(&out[0], &size, const_cast<char*>(data.data()), (unsigned int) data.size(), 9, 0, 0) != BZ_OK)
        throw runtime_error("bzip2 compression failed");
    out.resize(size);
    return out;
}

static long long octal(const char* field, int width) {
    long long value = 0;
    for (int i = 0; i < width && field[i]; i++)
        if (field[i] >= '0' && field[i] <= '7')
            value = 8*value+(field[i]-'0');
    return value;
}

// The ustar header checksum: the byte sum with the checksum field read as spaces.
static bool tarChecksumOk(const char* header) {
    unsigned sum = 0;
    for (int i = 0; i < 512; i++)
        sum += (i >= 148 && i < 156) ? ' ' : (unsigned char) header[i];
    return octal(header+148, 8) == (long long) sum;
}

static uint32_t bigEndian32(const char* p) {
    const unsigned char* b = (const unsigned char*) p;
    return (uint32_t) b[0] << 24 | (uint32_t) b[1] << 16 | (uint32_t) b[2] << 8 | (uint32_t) b[3];
}

optional<WuHeader> readWuHeader(const string& data) {
    if (data.size() < 1024 || tarChecksumOk(data.data()) || !tarChecksumOk(data.data()+512))
        return nullopt;
    const char* p = data.data();
    WuHeader h;
    h.version = bigEndian32(p);
    h.coreType = bigEndian32(p+0x04);
    h.clone = bigEndian32(p+0x34);
    h.gen = bigEndian32(p+0x38);
    h.run = bigEndian32(p+0x3c);
    h.payloadBytes = bigEndian32(p+0x64);
    h.project = bigEndian32(p+0x98);
    return h;
}

vector<pair<string, string>> readTar(const string& archive) {
    vector<pair<string, string>> members;
    string longName;
    size_t pos = 0;
    while (pos+512 <= archive.size()) {
        const char* header = archive.data()+pos;
        bool zero = true;
        for (int i = 0; i < 512 && zero; i++)
            zero = header[i] == 0;
        if (zero)
            break;
        if (!tarChecksumOk(header))
            throw runtime_error("tar header checksum wrong at offset "+to_string(pos));
        string name(header, strnlen(header, 100));
        if (memcmp(header+257, "ustar", 5) == 0 && header[345] != 0)
            name = string(header+345, strnlen(header+345, 155))+"/"+name;
        long long size = octal(header+124, 12);
        char type = header[156];
        pos += 512;
        if (pos+size > archive.size())
            throw runtime_error("tar member "+name+" is truncated");
        string body = archive.substr(pos, size);
        pos += (size+511)/512*512;
        if (type == 'L') {
            longName = string(body.c_str());
            continue;
        }
        if (!longName.empty()) {
            name = longName;
            longName.clear();
        }
        if (type == '0' || type == 0)
            members.push_back({name, body});
    }
    return members;
}

string writeTar(const vector<pair<string, string>>& members) {
    string out;
    for (auto& member : members) {
        if (member.first.size() > 99)
            throw runtime_error("tar member name too long: "+member.first);
        char header[512];
        memset(header, 0, sizeof(header));
        strncpy(header, member.first.c_str(), 99);
        snprintf(header+100, 8, "%07o", 0666);  // mode as core 0x27's results tar has it
        snprintf(header+108, 8, "%07o", 0);
        snprintf(header+116, 8, "%07o", 0);
        snprintf(header+124, 12, "%011llo", (unsigned long long) member.second.size());
        snprintf(header+136, 12, "%011llo", (unsigned long long) time(NULL));
        memset(header+148, ' ', 8);
        header[156] = '0';
        memcpy(header+257, "ustar", 6);
        header[263] = '0';  // ustar version "0"
        unsigned sum = 0;
        for (int i = 0; i < 512; i++)
            sum += (unsigned char) header[i];
        snprintf(header+148, 8, "%06o", sum);
        header[155] = ' ';
        out.append(header, 512);
        out.append(member.second);
        out.append((512-member.second.size()%512)%512, '\0');
    }
    out.append(1024, '\0');
    return out;
}

static string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == string::npos ? "" : s.substr(a, b-a+1);
}

map<string, string> parseCoreXml(const string& xml) {
    map<string, string> values;
    size_t pos = 0;
    while ((pos = xml.find('<', pos)) != string::npos) {
        if (xml.compare(pos, 4, "<!--") == 0) {
            size_t end = xml.find("-->", pos);
            pos = end == string::npos ? xml.size() : end+3;
            continue;
        }
        size_t close = xml.find('>', pos);
        if (close == string::npos)
            throw runtime_error("core.xml: unterminated tag");
        string tag = xml.substr(pos+1, close-pos-1);
        pos = close+1;
        if (tag.empty() || tag[0] == '?' || tag[0] == '/' || tag[0] == '!')
            continue;
        bool selfClosing = tag.back() == '/';
        if (selfClosing)
            tag.pop_back();
        string name = tag.substr(0, tag.find_first_of(" \t\r\n"));
        if (name == "config")
            continue;
        size_t v = tag.find(" v=");
        if (v != string::npos) {
            char quote = v+3 < tag.size() ? tag[v+3] : 0;
            size_t end = tag.find(quote, v+4);
            if ((quote != '"' && quote != '\'') || end == string::npos)
                throw runtime_error("core.xml: unquoted v= value for "+name);
            values[name] = trim(tag.substr(v+4, end-v-4));
        }
        else if (selfClosing)
            values[name] = "";
        if (!selfClosing) {
            string closing = "</"+name+">";
            size_t end = xml.find(closing, pos);
            if (end == string::npos)
                throw runtime_error("core.xml: no closing tag for "+name);
            if (v == string::npos)
                values[name] = trim(xml.substr(pos, end-pos));
            pos = end+closing.size();
        }
    }
    return values;
}

static uint64_t fnv1a(const void* data, size_t size) {
    uint64_t hash = 1469598103934665603ULL;
    const unsigned char* bytes = (const unsigned char*) data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void writeAll(int fd, const char* data, size_t size, const string& path) {
    while (size > 0) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            throw FileError("failed writing "+path+": "+strerror(errno));
        data += n;
        size -= (size_t) n;
    }
}

// F_FULLFSYNC: on macOS fsync leaves the data in the drive's cache.
static void writeSynced(const string& tmp, const string& data, const void* footer, size_t footerSize) {
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        throw FileError("cannot write "+tmp+": "+strerror(errno));
    try {
        writeAll(fd, data.data(), data.size(), tmp);
        writeAll(fd, (const char*) footer, footerSize, tmp);
        if (fcntl(fd, F_FULLFSYNC) != 0 && fsync(fd) != 0)
            throw FileError("cannot sync "+tmp+": "+strerror(errno));
    }
    catch (...) {
        close(fd);
        throw;
    }
    close(fd);
}

void writeAtomically(const string& path, const string& data, const string& previous) {
    string tmp = path+".tmp";
    uint64_t footer[2] = {data.size(), fnv1a(data.data(), data.size())};
    writeSynced(tmp, data, footer, sizeof(footer));
    if (!previous.empty() && fileExists(path) && rename(path.c_str(), previous.c_str()) != 0)
        throw FileError("cannot rename "+path+" to "+previous+": "+strerror(errno));
    if (rename(tmp.c_str(), path.c_str()) != 0)
        throw FileError("cannot rename "+tmp+" to "+path+": "+strerror(errno));
}

string readVerified(const string& path) {
    if (!fileExists(path))
        return "";
    string file = readFile(path);
    if (file.size() < 16)
        return "";
    uint64_t footer[2];
    memcpy(footer, file.data()+file.size()-16, 16);
    file.resize(file.size()-16);
    if (footer[0] != file.size() || footer[1] != fnv1a(file.data(), file.size()))
        return "";
    return file;
}

void replaceFile(const string& path, const string& data) {
    string tmp = path+".tmp";
    writeSynced(tmp, data, NULL, 0);
    if (rename(tmp.c_str(), path.c_str()) != 0)
        throw FileError("cannot rename "+tmp+" to "+path+": "+strerror(errno));
}
