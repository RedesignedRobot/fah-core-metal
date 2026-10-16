// SPDX-License-Identifier: MIT
// minicore: runs one FAH work unit the way a GPU core does, with OpenMM and its Metal platform linked in.
// Formats match core 0x27 8.2.1 as observed on project 18264 in October 2026; "core 0x27" below means
// that build.
//
// It takes the arguments fah-client passes a core (Unit.cpp:627-672 at fah-client-bastet 0ea081e, the same
// in master 706d6a0), picks the OpenMM platform from them, and passes the platform only the properties it
// declares. The work unit is <dir>/wudata_01.dat (a tar, or a bzip2 tar, of core.xml, system.xml,
// integrator.xml and state.xml, each plain or .bz2: the files a FAH work server sends core 0x26 per
// alchemiscale-fah docs/deployment.rst) or the same files loose in <dir>. core.xml sets the run the way
// openmm-core's documented options do (alchemiscale-fah FahOpenMMCoreSettings, perses make_core_file):
// numSteps, checkpointFreq, xtcFreq, viewerFreq, globalVarFreq (a negative interval is a percentage),
// precision, xtcAtoms, xtcMinAtom, xtcMaxAtom, forceTolerance, energyTolerance, maxRetriesFromLastCheckpoint,
// disableCheckpointStateTests, disableViewer, MWExclusionThreshold, DisablePmeStream, globalVarFilename,
// minimize, minimizeTolerance, minimizeMaxIterations.
//
// The run, in the order a live openmm-core 8 log shows it (core 0x23 8.0.3, fah-web-client-bastet#167/#168):
//   - state tests against CPU (as core 0x27 runs them; Reference if the build has no CPU platform) at the
//     start and at every checkpoint: first the unit's state.xml energies against CPU, then FAHBench's
//     StateTests (NaN in positions, velocities or forces; |v| > 17.47 nm/ps; more than half the velocity
//     components exactly 0; |F| > 50000 kJ/mol/nm; RMS of the force magnitude difference > forceTolerance;
//     |dPE| or |dKE| > energyTolerance), with FAHBench's messages;
//   - a non-finite energy or coordinate at an output step, an OpenMM exception while stepping, or a failed
//     state test is a bad state: "An exception occurred at step N: ...", then "ERROR:98: Attempting to
//     restart from last good checkpoint by restarting core." and exit CORE_RESTART, which makes the client
//     start the core again (Unit::finalizeRun, retry()). A checkpoint that fails its resume test is renamed
//     .failed, so the next start takes the one before it. The retry count lives on disk and resets at each
//     good checkpoint; past maxRetriesFromLastCheckpoint the core logs "ERROR:114: Max Retries Reached"
//     and exits BAD_WORK_UNIT;
//   - checkpoints are createCheckpoint() blobs written atomically with a length and FNV-1a footer, the
//     previous one kept. A restarted core takes the newest valid one, truncates positions.xtc, the globals
//     file and the viewer frames back to it, and state-tests it before stepping;
//   - positions.xtc frames (OpenMM's own xtc_utils writer) of the xtcAtoms selection, xtcAtoms.csv.bz2, a CSV
//     of integrator globals and context parameters, viewerTop.json and viewerFrameN.json in the layout
//     fah-web-client-bastet's Visualization.vue reads, and wuinfo_01.dat progress at every 1%;
//   - the log goes to <dir>/logfile_01.txt (the client copies it into its own log; it nulls the core's
//     stdout) and stdout, and science.log keeps every restart's log for the unit;
//   - SIGINT or SIGTERM (the client's stop; it kills 60 s later) checkpoints within about a second and exits
//     INTERRUPTED; the death of -lifeline does the same with CLIENT_DIED; a watchdog exits WU_STALLED if
//     stepping makes no progress for 600 s;
//   - at the end: checkpointIntegrator.xml, checkpointState.xml.bz2, positions.xtc, science.log,
//     xtcAtoms.csv.bz2 and any globals file, in core 0x27's order, as a plain tar in <dir>/wuresults_01.dat.
//
// Platform choice, first match wins:
//   --platform NAME                    override, for Reference or CPU control runs
//   -metal-platform P -metal-device D  Metal, device D (fah-client-bastet#459 with cbang#215)
//   -gpu-vendor apple                  Metal, device -opencl-device or -gpu (what client master sends)
//   -gpu-platform cuda|opencl|hip      that platform, if this build has it; else BAD_ARGUMENTS
//
// Exit codes are the client's (ExitCode.h). Where the public record shows the real core's choice (98, 114)
// this follows it. The real wudata/wuresults containers carry FAH's signatures, which this core neither
// checks nor writes.

#include "OpenMM.h"
#include "wufiles.h"
#include "xtc.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mach/mach.h>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace OpenMM;
using namespace std;

enum ExitCode {
    CORE_RESTART = 98, FINISHED_UNIT = 100, INTERRUPTED = 102, CLIENT_DIED = 103, BAD_WORK_UNIT = 114,
    BAD_ARGUMENTS = 115, MISSING_WORK_FILES = 116, FILE_IO_ERROR = 117, UNKNOWN_ERROR = 121,
    GPU_INITIALIZATION_ERROR = 125, GPU_UNAVAILABLE_ERROR = 126, WU_STALLED = 127
};

static const char* exitName(int code) {
    switch (code) {
    case CORE_RESTART: return "CORE_RESTART";
    case FINISHED_UNIT: return "FINISHED_UNIT";
    case INTERRUPTED: return "INTERRUPTED";
    case CLIENT_DIED: return "CLIENT_DIED";
    case BAD_WORK_UNIT: return "BAD_WORK_UNIT";
    case BAD_ARGUMENTS: return "BAD_ARGUMENTS";
    case MISSING_WORK_FILES: return "MISSING_WORK_FILES";
    case FILE_IO_ERROR: return "FILE_IO_ERROR";
    case GPU_INITIALIZATION_ERROR: return "GPU_INITIALIZATION_ERROR";
    case GPU_UNAVAILABLE_ERROR: return "GPU_UNAVAILABLE_ERROR";
    case WU_STALLED: return "WU_STALLED";
    default: return "UNKNOWN_ERROR";
    }
}

struct Failure : runtime_error {
    int code;
    Failure(int code, const string& message) : runtime_error(message), code(code) {}
};

static const auto processStart = chrono::steady_clock::now();
static const double StallSeconds = 600;
// Stepping goes in chunks sized to take between these, so a stop or a dead lifeline is seen quickly.
static const double ChunkSecondsMin = 0.25, ChunkSecondsMax = 1;
static const long long ChunkStepsMax = 1 << 20;

struct Options {
    string dir, suffix = "01", clientVersion, gpuPlatform, gpuVendor, platform;
    map<string, int> computeDevice;  // "opencl" -> -opencl-device, ...
    int gpu = -1;
    long lifeline = 0;
    double checkpointMinutes = 0;
    bool info = false;
};

// The run as core.xml sets it. Intervals are in steps; 0 disables an output.
struct Settings {
    long long steps = 0, checkpoint = 0, xtc = 0, viewer = 0, globals = 0;
    string precision = "single", globalsFile = "globals.csv", xtcAtoms;
    long long xtcMinAtom = -1, xtcMaxAtom = -1;
    double forceTolerance = 5, energyTolerance = 10, mwExclusion = 0, minimizeTolerance = 10;
    int maxRetries = 2, minimizeIterations = 5000;
    bool checkpointTests = true, disablePmeStream = true, minimize = false;
};

// The work unit's files by name, with any .bz2 suffix removed and the content unpacked.
typedef map<string, string> WorkFiles;

// The unit as read, and what writeResults needs to know about it.
struct WorkUnit {
    WorkFiles files;
    optional<WuHeader> header;
    string stateFileName = "state.xml";  // or state.xml.bz2, the unit's own name for it
    string globalsFileName;              // core.xml globalVarFilename, once read
};

// Until stepping starts nothing needs saving, so a stop exits at once. The client SIGKILLs a core about
// 60 s after SIGINT, and loading a large unit plus its first state test can take longer than that.
static atomic<bool> interrupted(false), deferSignals(false);
static int logFds[2] = {-1, -1};

// write(2) only: this runs in a signal handler. Apple clang honours the void cast for warn_unused_result.
static void writeAll(const char* text) {
    for (int fd : {STDOUT_FILENO, logFds[0], logFds[1]})
        if (fd >= 0)
            (void) write(fd, text, strlen(text));
}

static void onSignal(int number) {
    interrupted = true;
    writeAll(number == SIGINT ? "Caught signal SIGINT(2)\n" : "Caught signal SIGTERM(15)\n");
    writeAll("Exiting, please wait. . .\n");  // core 0x27's lines for a stop during start-up
    if (!deferSignals) {
        writeAll("ERROR:102: Core startup was interrupted by client.\nFolding@home Core Shutdown: INTERRUPTED\n");
        _exit(INTERRUPTED);
    }
}

// The log: stdout, <dir>/logfile_01.txt (this process) and <dir>/science.log (every process of the unit).
// Core 0x27 keeps two levels: logfile_01.txt, which the client copies into its own log, has the banner,
// unit, intervals, platforms, context, progress, checkpoints, average speed, saved files, errors and the
// shutdown line; science.log has those and everything else. say() writes both, note() only science.log.
static FILE* logFile = NULL;
static FILE* scienceLog = NULL;

static void logLine(bool client, const char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    string line(vsnprintf(NULL, 0, format, copy), '\0');
    va_end(copy);
    vsnprintf(&line[0], line.size()+1, format, args);
    line += '\n';
    fputs(line.c_str(), stdout);
    for (FILE* f : {client ? logFile : NULL, scienceLog})
        if (f != NULL) {
            fputs(line.c_str(), f);
            fflush(f);
        }
}

static void say(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logLine(true, format, args);
    va_end(args);
}

static void note(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void note(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logLine(false, format, args);
    va_end(args);
}

static void openLogs(const string& dir, const string& suffix) {
    logFile = fopen((dir+"/logfile_"+suffix+".txt").c_str(), "a");
    scienceLog = fopen((dir+"/science.log").c_str(), "a");
    if (logFile == NULL || scienceLog == NULL)
        throw FileError("cannot open the log files in "+dir+": "+strerror(errno));
    logFds[0] = fileno(logFile);
    logFds[1] = fileno(scienceLog);
}

static void readWorkUnit(const Options& opt, WorkUnit& unit) {
    string wudata = opt.dir+"/wudata_"+opt.suffix+".dat";
    auto add = [&](const string& name, const string& data) {
        bool packed = name.size() > 4 && name.compare(name.size()-4, 4, ".bz2") == 0;
        string key = packed ? name.substr(0, name.size()-4) : name;
        if (unit.files.count(key))
            throw runtime_error("the work unit has two members named "+key);
        unit.files[key] = packed ? bunzip2(data, name) : data;
        if (name == "state.xml.bz2")
            unit.stateFileName = name;
    };
    try {
        if (fileExists(wudata)) {
            string archive = readFile(wudata);
            // A unit from a FAH work server: a 512-byte header, then the tar. Core 0x27 logs "Project: P
            // (Run R, Clone C, Gen G)" first, then each member, then "Digital signatures verified". This
            // core reads the plain fields only; checking the digests and signature needs FAH's key.
            unit.header = readWuHeader(archive);
            if (unit.header) {
                const WuHeader& h = *unit.header;
                say("Project: %u (Run %u, Clone %u, Gen %u)", h.project, h.run, h.clone, h.gen);
                if (h.payloadBytes != archive.size()-512)
                    throw runtime_error("wudata header says "+to_string(h.payloadBytes)+" payload bytes, the file has "+
                                        to_string(archive.size()-512));
                archive.erase(0, 512);
            }
            if (isBzip2(archive))
                archive = bunzip2(archive, wudata);
            for (auto& member : readTar(archive)) {
                string name = member.first.substr(member.first.rfind('/')+1);
                if (name.compare(0, 2, "._") == 0)
                    continue;  // AppleDouble metadata that macOS tar adds
                note("Reading tar file %s", name.c_str());
                add(name, member.second);
            }
            if (unit.header)
                say("Digital signatures not checked: version %u header for core 0x%02x, signature scheme private to FAH",
                    unit.header->version, unit.header->coreType);
        }
        else
            for (const char* name : {"core.xml", "system.xml", "integrator.xml", "state.xml"}) {
                string path = opt.dir+"/"+name;
                if (fileExists(path))
                    add(name, readFile(path));
                else if (fileExists(path+".bz2"))
                    add(string(name)+".bz2", readFile(path+".bz2"));
            }
    }
    catch (const runtime_error& e) {
        throw Failure(BAD_WORK_UNIT, e.what());
    }
    for (const char* name : {"system.xml", "integrator.xml", "state.xml"})
        if (unit.files.count(name) == 0)
            throw Failure(MISSING_WORK_FILES, string("the work unit has no ")+name+" or "+name+".bz2");
}

template <class T>
static unique_ptr<T> deserialize(const WorkFiles& files, const string& name) {
    stringstream in(files.at(name));
    return unique_ptr<T>(XmlSerializer::deserialize<T>(in));
}

// An interval from core.xml: N steps, or -P for P percent of the unit. Project 18264 writes the percent
// with its sign, checkpointFreq v='-5%', which core 0x27 reads as 5% ("62500 steps (5%) [20 total]");
// a trailing % is taken the same way with or without the minus.
static long long interval(const map<string, string>& config, const string& key, long long fallback, long long steps) {
    long long value = fallback;
    auto entry = config.find(key);
    if (entry != config.end()) {
        const char* text = entry->second.c_str();
        char* end;
        value = strtoll(text, &end, 10);
        if (*end == '%' && end[1] == 0 && end != text)
            value = -llabs(value);
        else if (*end != 0)
            throw Failure(BAD_WORK_UNIT, "core.xml: bad "+key+" "+entry->second);
    }
    if (value < 0)
        value = max(1LL, steps*(-value)/100);
    return value;
}

static double configNumber(const map<string, string>& config, const string& key, double fallback) {
    auto entry = config.find(key);
    if (entry == config.end())
        return fallback;
    char* end;
    double value = strtod(entry->second.c_str(), &end);
    if (*end != 0 || end == entry->second.c_str())
        throw Failure(BAD_WORK_UNIT, "core.xml: bad "+key+" "+entry->second);
    return value;
}

static string configText(const map<string, string>& config, const string& key, const string& fallback) {
    auto entry = config.find(key);
    return entry == config.end() ? fallback : entry->second;
}

// The globals file shares the work directory with the core's own files and must not be one of them.
static bool isCoreFileName(const string& name) {
    static const char* const prefixes[] = {"wudata_", "wuresults_", "wuinfo_", "logfile_", "checkpoint",
        "viewerFrame", "viewerTop", "core.xml", "system.xml", "integrator.xml", "state.xml", "positions.xtc",
        "science.log", "xtcAtoms.csv", "retries.txt"};
    for (const char* prefix : prefixes)
        if (name.compare(0, strlen(prefix), prefix) == 0)
            return true;
    return false;
}

static Settings readSettings(const Options& opt, const WorkFiles& files) {
    map<string, string> config;
    if (files.count("core.xml")) {
        try {
            config = parseCoreXml(files.at("core.xml"));
        }
        catch (const runtime_error& e) {
            throw Failure(BAD_WORK_UNIT, e.what());
        }
    }
    static const set<string> known = {"numSteps", "checkpointFreq", "xtcFreq", "viewerFreq", "globalVarFreq",
        "globalVarFilename", "precision", "xtcAtoms", "xtcMinAtom", "xtcMaxAtom", "forceTolerance", "energyTolerance",
        "maxRetriesFromLastCheckpoint", "disableCheckpointStateTests", "disableViewer", "MWExclusionThreshold",
        "DisablePmeStream", "minimize", "minimizeTolerance", "minimizeMaxIterations", "minimizeReportFreq", "trrFreq",
        "trrMinAtom", "trrMaxAtom", "trrAtoms", "saveForcesInTrr"};
    for (auto& entry : config)
        if (known.count(entry.first) == 0)
            say("core.xml: ignoring unknown option %s", entry.first.c_str());
    // TRR output: alchemiscale-fah exposes it with trrFreq 0 by default, and no public project turns it
    // on. Refuse a unit that asks for it rather than drop the data it wants.
    if (configNumber(config, "trrFreq", 0) != 0)
        throw Failure(BAD_WORK_UNIT, "core.xml asks for TRR output (trrFreq), which this core does not write");
    Settings s;
    s.steps = (long long) configNumber(config, "numSteps", 0);
    if (s.steps <= 0)
        throw Failure(BAD_WORK_UNIT, "core.xml has no numSteps");
    s.checkpoint = interval(config, "checkpointFreq", -5, s.steps);
    if (s.checkpoint <= 0)
        s.checkpoint = s.steps;
    s.xtc = interval(config, "xtcFreq", 0, s.steps);
    s.viewer = interval(config, "viewerFreq", -1, s.steps);
    if (configNumber(config, "disableViewer", 0) != 0)
        s.viewer = 0;
    s.globals = interval(config, "globalVarFreq", 0, s.steps);
    s.globalsFile = configText(config, "globalVarFilename", s.globalsFile);
    if (s.globalsFile.empty() || s.globalsFile[0] == '.' || s.globalsFile.find('/') != string::npos ||
        isCoreFileName(s.globalsFile))
        throw Failure(BAD_WORK_UNIT, "core.xml: globalVarFilename "+s.globalsFile+" is not a file name of its own");
    s.precision = configText(config, "precision", s.precision);
    s.xtcAtoms = configText(config, "xtcAtoms", s.xtcAtoms);
    s.xtcMinAtom = (long long) configNumber(config, "xtcMinAtom", s.xtcMinAtom);
    s.xtcMaxAtom = (long long) configNumber(config, "xtcMaxAtom", s.xtcMaxAtom);
    s.forceTolerance = configNumber(config, "forceTolerance", s.forceTolerance);
    s.energyTolerance = configNumber(config, "energyTolerance", s.energyTolerance);
    s.maxRetries = (int) configNumber(config, "maxRetriesFromLastCheckpoint", s.maxRetries);
    s.checkpointTests = configNumber(config, "disableCheckpointStateTests", 0) == 0;
    s.mwExclusion = configNumber(config, "MWExclusionThreshold", s.mwExclusion);
    s.disablePmeStream = configNumber(config, "DisablePmeStream", s.disablePmeStream) != 0;
    s.minimize = configNumber(config, "minimize", s.minimize) != 0;
    s.minimizeTolerance = configNumber(config, "minimizeTolerance", s.minimizeTolerance);
    s.minimizeIterations = (int) configNumber(config, "minimizeMaxIterations", s.minimizeIterations);
    return s;
}

static string describeInterval(long long interval, long long steps) {
    if (interval <= 0)
        return "disabled";
    char text[128];
    snprintf(text, sizeof(text), "%lld steps (%g%%) [%lld total]", interval, 100.0*interval/steps, steps/interval);
    return text;
}

// The core type wuinfo_01.dat carries: the WU header's, else 0x24. The client drops the progress of a core
// whose type isn't the one FAH assigned the unit to (Unit.cpp:709-727).
static uint32_t coreType(const WorkUnit& unit) {
    return unit.header ? unit.header->coreType : 0x24;
}

// wuinfo_01.dat as core 0x27 writes it: 512 bytes, u32 type, the name "Core27 p18264" at 4, u32 total 100
// at 84, u32 done in percent at 88, u32 steps per percent at 92, the rest zero. The client reads
// {type; 80 reserved bytes; total; done}.
static void writeProgress(const Options& opt, const WorkUnit& unit, const Settings& s, long long done) {
    char info[512] = {};
    uint32_t type = coreType(unit), total = 100, percent = (uint32_t) (min(done, s.steps)*100/s.steps),
             perPercent = (uint32_t) max(1LL, s.steps/100);
    memcpy(info, &type, 4);
    snprintf(info+4, 80, "Core%02x p%u", type, unit.header ? unit.header->project : 0);
    memcpy(info+84, &total, 4);
    memcpy(info+88, &percent, 4);
    memcpy(info+92, &perPercent, 4);
    replaceFile(opt.dir+"/wuinfo_"+opt.suffix+".dat", string(info, sizeof(info)));
}

// The footprint Activity Monitor shows, so a leak over a multi-hour unit shows up in the log.
static double footprintMB() {
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t) &info, &count) != KERN_SUCCESS)
        return -1;
    return info.phys_footprint/1048576.0;
}

static void usage(const string& why) {
    cerr << "minicore: " << why << "\n"
            "usage: minicore -dir DIR [client arguments] [--platform NAME]\n"
            "       minicore --info\n"
            "client arguments (fah-client 8.5): -dir DIR -suffix S -version V -lifeline PID -gpu-uuid U\n"
            "    -gpu-platform cuda|opencl -gpu-vendor TYPE -<opencl|cuda|hip|metal>-platform N\n"
            "    -<opencl|cuda|hip|metal>-device N -gpu N -np N -checkpoint MINUTES\n"
            "--platform NAME runs on that OpenMM platform (Reference, CPU) instead of the GPU\n"
            "--info prints the OpenMM version and platforms\n";
    exit(BAD_ARGUMENTS);
}

static long long parseNumber(const string& name, const char* text) {
    char* end;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || *end != 0 || end == text)
        usage("bad number for "+name+": "+text);
    return value;
}

static int parseInt(const string& name, const char* text) {
    long long value = parseNumber(name, text);
    if (value < INT_MIN || value > INT_MAX)
        usage(name+" out of range: "+text);
    return (int) value;
}

static double parseReal(const string& name, const char* text) {
    char* end;
    errno = 0;
    double value = strtod(text, &end);
    if (errno != 0 || *end != 0 || end == text || value < 0)
        usage("bad number for "+name+": "+text);
    return value;
}

// -opencl-device, -cuda-platform and the like: one dash, an API name, then the suffix.
static bool isApiArg(const string& arg, const string& suffix) {
    return arg.size() > suffix.size()+1 && arg[0] == '-' && arg[1] != '-' &&
           arg.compare(arg.size()-suffix.size(), suffix.size(), suffix) == 0;
}

static Options parseOptions(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--info" || arg == "-info") {
            opt.info = true;
            continue;
        }
        if (i+1 >= argc)
            usage(arg+" needs a value");
        const char* value = argv[++i];
        if (arg == "-dir") opt.dir = value;
        else if (arg == "-suffix") opt.suffix = value;
        else if (arg == "-version") opt.clientVersion = value;
        else if (arg == "-lifeline") opt.lifeline = (long) parseNumber(arg, value);
        else if (arg == "-gpu-platform") opt.gpuPlatform = value;
        else if (arg == "-gpu-vendor") opt.gpuVendor = value;
        else if (arg == "-gpu") opt.gpu = parseInt(arg, value);
        else if (arg == "-checkpoint") opt.checkpointMinutes = parseReal(arg, value);
        else if (isApiArg(arg, "-device")) opt.computeDevice[arg.substr(1, arg.size()-8)] = parseInt(arg, value);
        else if (arg == "-gpu-uuid" || arg == "-np" || isApiArg(arg, "-platform")) {}  // accepted, unused
        else if (arg == "--platform") opt.platform = value;
        else usage("unknown argument "+arg);
    }
    if (!opt.info && opt.dir.empty())
        usage("-dir is required");
    return opt;
}

// The OpenMM platform and device the client's arguments ask for, and why.
struct PlatformChoice {
    string platform, reason;
    int device = 0;
};

static PlatformChoice choosePlatform(const Options& opt) {
    static const map<string, string> PlatformNames = {{"cuda", "CUDA"}, {"opencl", "OpenCL"}, {"hip", "HIP"}};
    PlatformChoice c;
    if (!opt.platform.empty()) {
        c.platform = opt.platform;
        c.reason = "--platform";
    }
    else if (opt.computeDevice.count("metal")) {
        c.platform = "Metal";
        c.device = opt.computeDevice.at("metal");
        c.reason = "client passed -metal-platform/-metal-device";
    }
    else if (opt.gpuVendor == "apple") {
        c.platform = "Metal";
        c.device = opt.computeDevice.count("opencl") ? opt.computeDevice.at("opencl") : max(0, opt.gpu);
        c.reason = "client passed -gpu-vendor apple";
    }
    else if (PlatformNames.count(opt.gpuPlatform)) {
        c.platform = PlatformNames.at(opt.gpuPlatform);
        c.device = opt.computeDevice.count(opt.gpuPlatform) ? opt.computeDevice.at(opt.gpuPlatform) : max(0, opt.gpu);
        c.reason = "client passed -gpu-platform "+opt.gpuPlatform+" -gpu-vendor "+opt.gpuVendor;
    }
    else {
        c.platform = "Metal";
        c.reason = "no GPU arguments; Metal is the only GPU platform in this core";
    }
    return c;
}

// The property map a FAH core would pass, reduced to the names the platform declares: Context
// creation throws "Illegal property name" on any other (ContextImpl.cpp:100-101).
static map<string, string> chooseProperties(const Settings& s, const PlatformChoice& choice, Platform& platform) {
    map<string, string> wanted;
    wanted["Precision"] = s.precision;
    wanted["DeviceIndex"] = to_string(choice.device);
    if (s.disablePmeStream)
        wanted["DisablePmeStream"] = "true";
    const vector<string>& names = platform.getPropertyNames();
    map<string, string> properties;
    for (auto& p : wanted) {
        if (find(names.begin(), names.end(), p.first) != names.end())
            properties[p.first] = p.second;
        else
            note("property %s not offered by %s, not passed", p.first.c_str(), platform.getName().c_str());
    }
    if (properties.count("DeviceIndex") == 0 && choice.device != 0 && platform.getName() == "Metal")
        throw Failure(GPU_UNAVAILABLE_ERROR, "the Metal platform has no DeviceIndex property and uses the system GPU; "
                      "the client asked for device "+to_string(choice.device));
    return properties;
}

// FAHBench StateTests limits.
static const double MaxVelocity = 17.47;  // nm/ps
static const double MaxForce = 50000;     // kJ/mol/nm

struct StateTestResult {
    string error, detail;
    bool compared = false;  // reached the reference comparison, so the numbers below are set
    double maxForceError = 0, rmsForceError = 0, magnitudeRmse = 0, dPE = 0, dKE = 0;
    double referencePE = 0, givenPE = 0, referenceKE = 0, givenKE = 0;
    bool ok() const { return error.empty(); }
};

static StateTestResult& fail(StateTestResult& r, const char* format, ...) __attribute__((format(printf, 2, 3)));
static StateTestResult& fail(StateTestResult& r, const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    r.error = text;
    return r;
}

// FAHBench's StateTests (fahbench/StateTests.cpp), run on the GPU state against a CPU (or Reference)
// Context of the same System and integrator, plus max |dF| and the RMS of the force vector difference for
// the record. The reference Context is built here so the unit's XML can be freed after start-up.
class StateTester {
public:
    StateTester(const WorkFiles& files, const Settings& settings, const string& platformName)
        : settings(settings), refSystem(deserialize<System>(files, "system.xml")),
          refIntegrator(deserialize<Integrator>(files, "integrator.xml")),
          refContext(new Context(*refSystem, *refIntegrator, Platform::getPlatformByName(platformName))) {}

    // The unit's state.xml carries the energies its last core computed and no forces, so this compares
    // energies only. Empty if state.xml has no energies.
    optional<StateTestResult> compareInitial(const State& given) {
        StateTestResult r;
        try {
            r.givenPE = given.getPotentialEnergy();
            r.givenKE = given.getKineticEnergy();
        }
        catch (const OpenMMException&) {
            return nullopt;
        }
        refContext->setState(given);
        State expected = refContext->getState(State::Energy);
        r.referencePE = expected.getPotentialEnergy();
        r.referenceKE = expected.getKineticEnergy();
        checkEnergies(r);
        return r;
    }

    StateTestResult test(Context& gpuContext, const Integrator& integrator, bool zeroVelocityTest) {
        StateTestResult r;
        State gpu = gpuContext.getState(State::Positions | State::Velocities | State::Forces | State::Energy | State::Parameters);
        const vector<Vec3>& x = gpu.getPositions();
        const vector<Vec3>& v = gpu.getVelocities();
        const vector<Vec3>& f = gpu.getForces();
        // FAHBench checkForNans
        const pair<const vector<Vec3>*, const char*> arrays[] = {{&x, "positions"}, {&v, "velocities"}, {&f, "forces"}};
        for (auto& array : arrays)
            for (size_t i = 0; i < array.first->size(); i++)
                for (int j = 0; j < 3; j++)
                    if (std::isnan((*array.first)[i][j]))
                        return fail(r, "NaNs detected in %s: (%zu, %d)", array.second, i, j);
        // FAHBench checkForDiscrepancies
        size_t zeroVelocities = 0;
        for (size_t i = 0; i < v.size(); i++)
            for (int j = 0; j < 3; j++) {
                if (fabs(v[i][j]) > MaxVelocity)
                    return fail(r, "Discrepancy: Velocities are blowing up: (%zu, %d) = %g", i, j, v[i][j]);
                if (v[i][j] == 0)
                    zeroVelocities++;
            }
        if (zeroVelocityTest && zeroVelocities > 3*v.size()/2)
            return fail(r, "Discrepancy: More than half of all velocities set exactly to zero!");
        for (size_t i = 0; i < f.size(); i++)
            for (int j = 0; j < 3; j++)
                if (fabs(f[i][j]) > MaxForce)  // core 0x23's wording, not FAHBench's "(i, j)"
                    return fail(r, "Discrepancy: Forces are blowing up! %zu %d", i, j);
        // FAHBench compareForces and compareEnergies, against the test platform
        Vec3 a, b, c;
        gpu.getPeriodicBoxVectors(a, b, c);
        refContext->setPeriodicBoxVectors(a, b, c);
        refContext->setPositions(x);
        refContext->setVelocities(v);
        for (auto& param : gpu.getParameters())
            refContext->setParameter(param.first, param.second);
        if (const CustomIntegrator* custom = dynamic_cast<const CustomIntegrator*>(&integrator)) {
            CustomIntegrator& refCustom = dynamic_cast<CustomIntegrator&>(*refIntegrator);
            for (int i = 0; i < custom->getNumGlobalVariables(); i++)
                refCustom.setGlobalVariable(i, custom->getGlobalVariable(i));
        }
        State expected = refContext->getState(State::Forces | State::Energy);
        const vector<Vec3>& g = expected.getForces();
        double mse = 0, sumSquares = 0;
        for (size_t i = 0; i < f.size(); i++) {
            double error = sqrt(g[i].dot(g[i]))-sqrt(f[i].dot(f[i]));
            mse += error*error;
            Vec3 d = f[i]-g[i];
            r.maxForceError = max(r.maxForceError, sqrt(d.dot(d)));
            sumSquares += d.dot(d);
        }
        size_t n = max<size_t>(1, f.size());
        r.magnitudeRmse = sqrt(mse/n);
        r.rmsForceError = sqrt(sumSquares/n);
        r.referencePE = expected.getPotentialEnergy();
        r.givenPE = gpu.getPotentialEnergy();
        r.referenceKE = expected.getKineticEnergy();
        r.givenKE = gpu.getKineticEnergy();
        r.compared = true;
        if (!(r.magnitudeRmse <= settings.forceTolerance))
            fail(r, "Force RMSE error of %g with threshold of %g", r.magnitudeRmse, settings.forceTolerance);
        checkEnergies(r);
        return r;
    }

private:
    // !(error <= limit), so a NaN error fails too. An earlier failure stays the one reported.
    void checkEnergies(StateTestResult& r) const {
        r.dPE = fabs(r.referencePE-r.givenPE);
        r.dKE = fabs(r.referenceKE-r.givenKE);
        if (!r.ok())
            return;
        char text[256];
        if (!(r.dPE <= settings.energyTolerance)) {
            fail(r, "Potential energy error of %g, threshold of %g", r.dPE, settings.energyTolerance);
            snprintf(text, sizeof(text), "Reference Potential Energy: %g | Given Potential Energy: %g", r.referencePE, r.givenPE);
            r.detail = text;
        }
        else if (!(r.dKE <= settings.energyTolerance)) {
            fail(r, "Kinetic energy error of %g, threshold of %g", r.dKE, settings.energyTolerance);
            snprintf(text, sizeof(text), "Reference Kinetic Energy: %g | Given Kinetic Energy: %g", r.referenceKE, r.givenKE);
            r.detail = text;
        }
    }

    const Settings& settings;
    unique_ptr<System> refSystem;
    unique_ptr<Integrator> refIntegrator;
    unique_ptr<Context> refContext;
};

static bool isFinite(const State& state) {
    if (!isfinite(state.getPotentialEnergy()+state.getKineticEnergy()))
        return false;
    for (const Vec3& p : state.getPositions())
        if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2]))
            return false;
    return true;
}

static double since(chrono::steady_clock::time_point start) {
    return chrono::duration<double>(chrono::steady_clock::now()-start).count();
}

// Water: a molecule of three or four particles weighing 18.0 +- 0.6 amu with no particle heavier than
// oxygen. Hydrogen mass repartitioning leaves water alone in OpenMM's app layer.
static bool isWater(const System& system, const vector<int>& molecule) {
    if (molecule.size() < 3 || molecule.size() > 4)
        return false;
    double total = 0;
    for (int i : molecule) {
        double m = system.getParticleMass(i);
        if (m > 16.5)
            return false;
        total += m;
    }
    return fabs(total-18.015) < 0.6;
}

// The atoms positions.xtc stores: xtcAtoms is "solute" (everything but water), a list of indices and
// ranges ("0-99,150"), or unset (all), then cut to [xtcMinAtom, xtcMaxAtom].
static vector<int> selectXtcAtoms(const Settings& s, const System& system, const vector<vector<int>>& molecules) {
    int n = system.getNumParticles();
    vector<char> keep(n, 1);
    if (s.xtcAtoms == "solute") {
        for (auto& molecule : molecules)
            if (isWater(system, molecule))
                for (int i : molecule)
                    keep[i] = 0;
    }
    else if (!s.xtcAtoms.empty()) {
        fill(keep.begin(), keep.end(), 0);
        stringstream list(s.xtcAtoms);
        string item;
        while (getline(list, item, ',')) {
            if (item.find_first_not_of(" \t") == string::npos)
                continue;
            char* end;
            long a = strtol(item.c_str(), &end, 10), b = a;
            if (*end == '-')
                b = strtol(end+1, &end, 10);
            while (*end == ' ')
                end++;
            if (*end != 0 || a < 0 || b < a || b >= n)
                throw Failure(BAD_WORK_UNIT, "core.xml: bad xtcAtoms entry "+item);
            for (long i = a; i <= b; i++)
                keep[i] = 1;
        }
    }
    vector<int> atoms;
    for (int i = 0; i < n; i++)
        if (keep[i] && (s.xtcMinAtom < 0 || i >= s.xtcMinAtom) && (s.xtcMaxAtom < 0 || i <= s.xtcMaxAtom))
            atoms.push_back(i);
    return atoms;
}

// Masses core 0x27 names in viewerTop.json. On P18264 it names C 12.01, O 16.00 and Cl 35.45 and leaves
// N 14.01, Na 22.99 and every repartitioned mass as "?". Its own table is private; this one, matched within
// 1e-4 amu, reproduces all 2520 atoms of that unit.
static const struct { const char* symbol; int number; double mass; } StandardMasses[] = {
    {"H", 1, 1.008}, {"C", 6, 12.01}, {"N", 7, 14.007}, {"O", 8, 16.00}, {"F", 9, 18.998}, {"Na", 11, 22.98977},
    {"P", 15, 30.974}, {"S", 16, 32.06}, {"Cl", 17, 35.45}, {"K", 19, 39.098}};

// viewerTop.json for fah-web-client-bastet's Visualization.vue, byte for byte in core 0x27's layout:
// units "A", atoms as [name, 0, 0, 0, number] one a line, then bonds as [a,b] over the shown atoms. The
// shown atoms are every massive particle outside water ("[ Finding non-water atoms ]", then "atoms above 0
// Da"), here also only in molecules at or above MWExclusionThreshold. An atom with a standard mass carries
// its symbol and atomic number, any other reads ["?",0,0,0,trunc(mass)]. The bonds are the HarmonicBondForce
// bonds and constraints between shown atoms, sorted and without repeats.
static vector<int> writeViewerTopology(const string& path, const Settings& s, const System& system,
                                       const vector<vector<int>>& molecules) {
    vector<pair<int, int>> bonds;
    for (int f = 0; f < system.getNumForces(); f++)
        if (const HarmonicBondForce* hb = dynamic_cast<const HarmonicBondForce*>(&system.getForce(f)))
            for (int i = 0; i < hb->getNumBonds(); i++) {
                int a, b;
                double length, k;
                hb->getBondParameters(i, a, b, length, k);
                bonds.push_back({a, b});
            }
    for (int i = 0; i < system.getNumConstraints(); i++) {
        int a, b;
        double d;
        system.getConstraintParameters(i, a, b, d);
        bonds.push_back({a, b});
    }
    vector<int> shown, index(system.getNumParticles(), -1);
    for (auto& molecule : molecules) {
        double total = 0;
        for (int i : molecule)
            total += system.getParticleMass(i);
        if (total < s.mwExclusion || isWater(system, molecule))
            continue;
        for (int i : molecule)
            if (system.getParticleMass(i) > 0)
                shown.push_back(i);
    }
    sort(shown.begin(), shown.end());
    for (size_t i = 0; i < shown.size(); i++)
        index[shown[i]] = (int) i;
    ostringstream out;
    out << "{\n\"units\": \"A\",\"atoms\": [\n";
    for (size_t i = 0; i < shown.size(); i++) {
        double mass = system.getParticleMass(shown[i]);
        const char* symbol = "?";
        int number = (int) mass;
        for (auto& e : StandardMasses)
            if (fabs(e.mass-mass) < 1e-4) {
                symbol = e.symbol;
                number = e.number;
            }
        out << (i ? ",\n" : "") << "[\"" << symbol << "\",0,0,0," << number << "]";
    }
    out << "\n],\n \"bonds\": [\n";
    sort(bonds.begin(), bonds.end());
    bonds.erase(unique(bonds.begin(), bonds.end()), bonds.end());
    bool first = true;
    for (auto& bond : bonds)
        if (index[bond.first] >= 0 && index[bond.second] >= 0) {
            out << (first ? "" : ",\n") << "[" << index[bond.first] << "," << index[bond.second] << "]";
            first = false;
        }
    out << "\n  ]\n}";
    replaceFile(path, out.str());
    return shown;
}

// What a checkpoint records besides the OpenMM blob: the unit's step count from state.xml, and the sizes
// of the append-only outputs at that step, so a restarted core can cut off what the dead process wrote
// after it.
struct CheckpointMeta {
    long long step = 0, unitStep = 0, xtcBytes = 0, globalsBytes = 0;
    int viewerFrames = 0;
};

static const char CheckpointFormat[] = "minicore-checkpoint 3 step=%lld unit=%lld xtc=%lld globals=%lld viewer=%d\n";

static string checkpointHeader(const CheckpointMeta& meta) {
    char header[256];
    snprintf(header, sizeof(header), CheckpointFormat, meta.step, meta.unitStep, meta.xtcBytes, meta.globalsBytes,
             meta.viewerFrames);
    return header;
}

// Reads the header line off body, which leaves the OpenMM checkpoint.
static bool unpackCheckpoint(string& body, CheckpointMeta& meta) {
    size_t end = body.find('\n');
    if (end == string::npos || end > 255)
        return false;
    string line = body.substr(0, end+1);
    if (sscanf(line.c_str(), CheckpointFormat, &meta.step, &meta.unitStep, &meta.xtcBytes, &meta.globalsBytes,
               &meta.viewerFrames) != 5)
        return false;
    body.erase(0, end+1);
    return true;
}

static void truncateTo(const string& path, long long bytes) {
    long long size = fileSize(path);
    if (size > bytes) {
        if (truncate(path.c_str(), bytes) != 0)
            throw FileError("cannot truncate "+path+": "+strerror(errno));
        say("Truncated %s from %lld to %lld bytes (the last checkpoint)", path.c_str(), size, bytes);
    }
    else if (size >= 0 && size < bytes)
        say("%s has %lld bytes, the checkpoint recorded %lld", path.c_str(), size, bytes);
}

static int readCount(const string& path) {
    ifstream in(path);
    int n = 0;
    in >> n;
    return in ? n : 0;
}

static void writeCount(const string& path, int n) {
    replaceFile(path, to_string(n)+"\n");
}

// A short write throws, so a full disk ends the run instead of thinning the data.
static void appendLine(const string& path, const string& line) {
    ofstream out(path, ios::app);
    out << line << '\n';
    out.close();
    if (!out)
        throw FileError("cannot append to "+path);
}

static string viewerFramePath(const string& dir, int frame) {
    return dir+"/viewerFrame"+to_string(frame)+".json";
}

static void removeViewerFrames(const string& dir, int from) {
    for (int frame = from; fileExists(viewerFramePath(dir, frame)); frame++)
        unlink(viewerFramePath(dir, frame).c_str());
}

// Exits WU_STALLED if stepping makes no progress for StallSeconds. The client restarts a core that stops
// reporting progress (Unit::isStalled); a core that finds its own stall exits WU_STALLED and the client
// resumes it the same way (Unit::finalizeRun).
static atomic<bool> watchdogArmed(false);
static atomic<double> lastBeat(0);

static void beat() {
    lastBeat = since(processStart);
}

static void startWatchdog() {
    thread([]() {
        while (true) {
            this_thread::sleep_for(chrono::seconds(5));
            if (watchdogArmed && since(processStart)-lastBeat > StallSeconds) {
                say("Core stalled: no progress in %.0f s, exiting so the client restarts from the last checkpoint", StallSeconds);
                say("Folding@home Core Shutdown: %s", exitName(WU_STALLED));
                _exit(WU_STALLED);
            }
        }
    }).detach();
}

#ifdef MINICORE_STATIC
// link.sh links OpenMM and the Metal platform in, the way a shipped core does; there are no plugins.
extern "C" void registerMetalPlatform();
#ifdef MINICORE_CPU
extern "C" void registerCpuPlatform();
extern "C" void registerCpuPmeKernelFactories();
#endif
#endif

static void registerPlatforms() {
#ifdef MINICORE_STATIC
#ifdef MINICORE_CPU
    // The CPU platform runs the state tests, as core 0x27 does ("Using CPU platform for reference
    // calculations."). The CPU PME kernels register before Metal exists, so only Reference and CPU get them.
    registerCpuPlatform();
    registerCpuPmeKernelFactories();
#endif
    registerMetalPlatform();
#else
    Platform::loadPluginsFromDirectory(Platform::getDefaultPluginsDirectory());
#endif
    for (const string& failure : Platform::getPluginLoadFailures())
        printf("plugin load failure: %s\n", failure.c_str());
}

// One process's run of a unit: start or resume, step, checkpoint, finish.
class CoreRun {
public:
    CoreRun(const Options& opt, WorkUnit& unit)
        : opt(opt), unit(unit), checkpointPath(opt.dir+"/checkpoint.bin"), previousPath(opt.dir+"/checkpoint.prev.bin"),
          retriesPath(opt.dir+"/retries.txt"), xtcPath(opt.dir+"/positions.xtc") {}
    int run();

private:
    void createContext();
    void prepareOutputs();
    bool resume();
    void startFresh();
    int testAtStart(bool resumed);
    int stepToEnd();
    void writeFinalState();
    int badState(long long step, const string& error, const string& detail);
    bool runStateTest(long long step, bool zeroVelocityTest, StateTestResult& r);
    void saveCheckpoint(long long step);
    void writeFrames(long long step, const State& state, bool xtcFrame, bool viewerFrame, bool globalsRow);
    long long nextEvent(long long step) const;
    double nsPerDay(long long steps, double seconds) const;
    bool lifelineGone() const;

    const Options& opt;
    WorkUnit& unit;
    const string checkpointPath, previousPath, retriesPath, xtcPath;
    string globalsPath, testPlatform, loadedCheckpoint;
    Settings settings;
    long long progressInterval = 1;
    unique_ptr<System> system;
    unique_ptr<Integrator> integrator;
    CustomIntegrator* custom = NULL;
    unique_ptr<Context> context;
    unique_ptr<StateTester> tester;
    optional<StateTestResult> initial;
    CheckpointMeta meta;
    vector<int> xtcAtoms, viewerAtoms;
    vector<string> parameterNames;
    int retries = 0;
    double parseSeconds = 0, buildSeconds = 0;
    chrono::steady_clock::time_point buildEnd;
};

int CoreRun::run() {
    char started[64];
    time_t now = time(NULL);
    strftime(started, sizeof(started), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    say("*********************** Log Started %s ***********************", started);
    say("minicore (fah-core-metal), OpenMM %s", Platform::getOpenMMVersion().c_str());
    say("client arguments: dir %s, version %s, lifeline %ld, gpu-platform %s, gpu-vendor %s, gpu %d", opt.dir.c_str(),
        opt.clientVersion.c_str(), opt.lifeline, opt.gpuPlatform.c_str(), opt.gpuVendor.c_str(), opt.gpu);

    readWorkUnit(opt, unit);
    note("Core type 0x%02x%s", coreType(unit), unit.header ? " (from the WU header)" : "");
    settings = readSettings(opt, unit.files);
    unit.globalsFileName = settings.globalsFile;
    globalsPath = opt.dir+"/"+settings.globalsFile;
    progressInterval = max(1LL, settings.steps/100);
    say("  Checkpoint write interval: %s", describeInterval(settings.checkpoint, settings.steps).c_str());
    say("  JSON viewer frame write interval: %s", describeInterval(settings.viewer, settings.steps).c_str());
    say("  XTC frame write interval: %s", describeInterval(settings.xtc, settings.steps).c_str());
    say("  TRR frame write interval: disabled");
    say("  Global context and integrator variables write interval: %s", describeInterval(settings.globals, settings.steps).c_str());
    note("  Precision: %s; state test limits: force RMSE %g kJ/mol/nm, energy %g kJ/mol; checkpoint state tests %s; "
        "max retries from last checkpoint %d", settings.precision.c_str(), settings.forceTolerance,
        settings.energyTolerance, settings.checkpointTests ? "on" : "off", settings.maxRetries);

    createContext();
    prepareOutputs();
    bool resumed = resume();
    if (!resumed)
        startFresh();
    if (int code = testAtStart(resumed))
        return code;
    WorkFiles().swap(unit.files);
    int code = stepToEnd();
    watchdogArmed = false;
    if (code == FINISHED_UNIT)
        writeFinalState();
    return code;
}

void CoreRun::createContext() {
    // The platform list and context lines core 0x27 prints; the client parses none of them.
    say("There are %d platforms available.", Platform::getNumPlatforms());
    for (int i = 0; i < Platform::getNumPlatforms(); i++)
        say("Platform %d: %s", i, Platform::getPlatform(i).getName().c_str());
    PlatformChoice choice = choosePlatform(opt);
    Platform* platform;
    try {
        platform = &Platform::getPlatformByName(choice.platform);
    }
    catch (const OpenMMException&) {
        throw Failure(BAD_ARGUMENTS, choice.platform+" is not in this core ("+choice.reason+")");
    }
    say("platform %s device %d: %s", choice.platform.c_str(), choice.device, choice.reason.c_str());
    map<string, string> properties = chooseProperties(settings, choice, *platform);

    auto parseStart = chrono::steady_clock::now();
    try {
        system = deserialize<System>(unit.files, "system.xml");
        integrator = deserialize<Integrator>(unit.files, "integrator.xml");
    }
    catch (const exception& e) {
        throw Failure(BAD_WORK_UNIT, string("deserializing the work unit: ")+e.what());
    }
    custom = dynamic_cast<CustomIntegrator*>(integrator.get());
    parseSeconds = since(parseStart);
    auto buildStart = chrono::steady_clock::now();
    say("Attempting to create %s context:", choice.platform.c_str());
    say("  Configuring platform %s", choice.platform.c_str());
    try {
        context.reset(new Context(*system, *integrator, *platform, properties));
    }
    catch (const OpenMMException& e) {
        throw Failure(GPU_INITIALIZATION_ERROR, string("creating the Context: ")+e.what());
    }
    buildSeconds = since(buildStart);
    buildEnd = chrono::steady_clock::now();
    say("  Using %s on %s Platform and gpu %d", choice.platform.c_str(), choice.platform.c_str(), choice.device);
    note("%d atoms, %d forces, step size %g ps", system->getNumParticles(), system->getNumForces(), integrator->getStepSize());
    for (auto& property : properties)
        note("property %s = %s", property.first.c_str(), platform->getPropertyValue(*context, property.first).c_str());
}

void CoreRun::prepareOutputs() {
    vector<vector<int>> molecules = context->getMolecules();
    if (settings.xtc > 0) {
        xtcAtoms = selectXtcAtoms(settings, *system, molecules);
        note("XTC atoms: %zu of %d (%s)", xtcAtoms.size(), system->getNumParticles(),
             settings.xtcAtoms.empty() ? "all" : settings.xtcAtoms.c_str());
        // Core 0x27 writes xtcAtoms.csv.bz2 next to positions.xtc before the first step.
        ostringstream list;
        for (size_t i = 0; i < xtcAtoms.size(); i++)
            list << (i ? "," : "") << xtcAtoms[i];
        replaceFile(opt.dir+"/xtcAtoms.csv.bz2", bzip2(list.str()));
    }
    if (settings.viewer > 0) {
        viewerAtoms = writeViewerTopology(opt.dir+"/viewerTop.json", settings, *system, molecules);
        note("viewer topology: %zu atoms", viewerAtoms.size());
    }
    for (auto& param : context->getParameters()) {
        bool shadowed = false;
        for (int i = 0; custom && i < custom->getNumGlobalVariables(); i++)
            shadowed |= custom->getGlobalVariableName(i) == param.first;
        if (!shadowed)
            parameterNames.push_back(param.first);
    }

    testPlatform = "Reference";
    for (int i = 0; i < Platform::getNumPlatforms(); i++)
        if (Platform::getPlatform(i).getName() == "CPU")
            testPlatform = "CPU";
    note("  Configuring CPU/Reference platform for sanity checks...");
    note("    Using %s platform for reference calculations.", testPlatform.c_str());
    tester.reset(new StateTester(unit.files, settings, testPlatform));
}

bool CoreRun::resume() {
    for (const string& path : {checkpointPath, previousPath}) {
        string body = readVerified(path);
        CheckpointMeta saved;
        if (body.empty() || !unpackCheckpoint(body, saved)) {
            if (fileExists(path))
                say("%s failed its length, hash or header check, not using it", path.c_str());
            continue;
        }
        istringstream in(body);
        try {
            context->loadCheckpoint(in);
        }
        catch (const OpenMMException& e) {
            say("%s did not load (%s), not using it", path.c_str(), e.what());
            continue;
        }
        meta = saved;
        loadedCheckpoint = path;
        say("resumed from checkpoint at step %lld (%s)", context->getStepCount(), path.c_str());
        truncateTo(xtcPath, meta.xtcBytes);
        truncateTo(globalsPath, meta.globalsBytes);
        removeViewerFrames(opt.dir, meta.viewerFrames);
        return true;
    }
    return false;
}

void CoreRun::startFresh() {
    unique_ptr<State> state = deserialize<State>(unit.files, "state.xml");
    context->setState(*state);
    meta.unitStep = state->getStepCount();
    context->setStepCount(0);
    note("  Comparing forces and energies between initial State and %s...", testPlatform.c_str());
    initial = tester->compareInitial(*state);
    if (initial)
        note("    initial State PE %.4f, %s PE %.4f: |dPE| %.4g, |dKE| %.4g kJ/mol (limit %g)", initial->givenPE,
            testPlatform.c_str(), initial->referencePE, initial->dPE, initial->dKE, settings.energyTolerance);
    else
        note("    state.xml has no energies");
    unlink(xtcPath.c_str());
    unlink(globalsPath.c_str());
    removeViewerFrames(opt.dir, 0);
    if (settings.minimize) {
        say("Minimizing: tolerance %g kJ/mol/nm, at most %d iterations", settings.minimizeTolerance, settings.minimizeIterations);
        LocalEnergyMinimizer::minimize(*context, settings.minimizeTolerance, settings.minimizeIterations);
    }
    if (settings.globals > 0) {
        // The first column is the core's step count; it gets another name if the integrator has a
        // global called step (perses and alchemiscale-fah NEQ integrators do).
        string header = "step";
        for (int i = 0; custom && i < custom->getNumGlobalVariables(); i++)
            if (custom->getGlobalVariableName(i) == "step")
                header = "core_step";
        for (int i = 0; custom && i < custom->getNumGlobalVariables(); i++)
            header += ","+custom->getGlobalVariableName(i);
        for (auto& name : parameterNames)
            header += ","+name;
        appendLine(globalsPath, header);
    }
}

int CoreRun::testAtStart(bool resumed) {
    retries = readCount(retriesPath);
    long long step = context->getStepCount();
    StateTestResult r = initial ? *initial : StateTestResult();
    if (r.ok()) {
        note("  Comparing forces and energies between GPU and %s...", testPlatform.c_str());
        runStateTest(step, resumed, r);
    }
    if (!r.ok()) {
        if (resumed) {
            // The next start takes the older checkpoint, which passed a state test when it was written.
            if (rename(loadedCheckpoint.c_str(), (loadedCheckpoint+".failed").c_str()) != 0)
                say("cannot set %s aside: %s", loadedCheckpoint.c_str(), strerror(errno));
            return badState(step, r.error, r.detail);
        }
        // A unit that fails its first test is not retried. Cores 0x23 and 0x24 log the error with an ERROR:
        // prefix, return the logs and the unit's state file under its own name, and exit BAD_WORK_UNIT
        // (forum t=42176: "ERROR:Potential energy error of 296.63, threshold of 20" then "Saving result
        // file state.xml.bz2").
        say("ERROR:%s", r.error.c_str());
        if (!r.detail.empty())
            say("ERROR:%s", r.detail.c_str());
        const string& state = unit.files.at("state.xml");
        replaceFile(opt.dir+"/"+unit.stateFileName, unit.stateFileName == "state.xml" ? state : bzip2(state));
        return BAD_WORK_UNIT;
    }
    note("start-up: system and integrator XML %.1f s, context %.1f s, state or checkpoint load and first state test %.1f s, %.1f s since exec, footprint %.0f MB",
        parseSeconds, buildSeconds, since(buildEnd), since(processStart), footprintMB());
    if (!resumed) {
        // Core 0x27 writes viewerFrame0 at step 0 (101 frames for a 1% interval). It goes before the step-0
        // checkpoint so that the checkpoint's output sizes include it.
        if (settings.viewer > 0 || settings.globals > 0)
            writeFrames(0, context->getState(State::Positions), false, settings.viewer > 0, settings.globals > 0);
        saveCheckpoint(0);
        say("Completed 0 out of %lld steps (0%%)", settings.steps);
        say("Checkpoint completed at step 0");
    }
    writeProgress(opt, unit, settings, step);
    return 0;
}

// A bad state: log it as the live core does, then ask the client for a restart or give up.
int CoreRun::badState(long long step, const string& error, const string& detail) {
    say("An exception occurred at step %lld: %s", step, error.c_str());
    if (!detail.empty())
        say("%s", detail.c_str());
    writeCount(retriesPath, ++retries);
    if (retries > settings.maxRetries) {
        say("ERROR:114: Max Retries Reached");
        return BAD_WORK_UNIT;
    }
    say("ERROR:98: Attempting to restart from last good checkpoint by restarting core.");
    return CORE_RESTART;
}

bool CoreRun::runStateTest(long long step, bool zeroVelocityTest, StateTestResult& r) {
    note("  Running tests");
    r = tester->test(*context, *integrator, zeroVelocityTest);
    if (r.compared)
        note("State test at step %lld: max|dF| %.4g, rms dF %.4g, force RMSE %.4g kJ/mol/nm (limit %g), "
            "|dPE| %.4g, |dKE| %.4g kJ/mol (limit %g): %s", step, r.maxForceError, r.rmsForceError, r.magnitudeRmse,
            settings.forceTolerance, r.dPE, r.dKE, settings.energyTolerance, r.ok() ? "pass" : "FAIL");
    else
        say("State test at step %lld: FAIL", step);
    if (r.ok())
        note("  All tests passed.");
    return r.ok();
}

void CoreRun::saveCheckpoint(long long step) {
    meta.step = step;
    meta.xtcBytes = max(0LL, fileSize(xtcPath));
    meta.globalsBytes = max(0LL, fileSize(globalsPath));
    ostringstream data;
    data << checkpointHeader(meta);
    context->createCheckpoint(data);
    writeAtomically(checkpointPath, data.str(), previousPath);
}

void CoreRun::writeFrames(long long step, const State& state, bool xtcFrame, bool viewerFrame, bool globalsRow) {
    if (globalsRow) {
        ostringstream row;
        row.precision(17);
        row << step;
        for (int i = 0; custom && i < custom->getNumGlobalVariables(); i++)
            row << "," << custom->getGlobalVariable(i);
        for (auto& name : parameterNames)
            row << "," << context->getParameter(name);
        appendLine(globalsPath, row.str());
    }
    // Unwrapped, as core 0x27 writes positions.xtc and the viewer frames.
    const vector<Vec3>& x = state.getPositions();
    if (xtcFrame) {
        vector<float> positions(3*xtcAtoms.size()), box(9);
        for (size_t i = 0; i < xtcAtoms.size(); i++)
            for (int j = 0; j < 3; j++)
                positions[3*i+j] = (float) x[xtcAtoms[i]][j];
        Vec3 a, b, c;
        state.getPeriodicBoxVectors(a, b, c);
        for (int j = 0; j < 3; j++) {
            box[j] = (float) a[j];
            box[3+j] = (float) b[j];
            box[6+j] = (float) c[j];
        }
        int frameStep = (int) step;
        float time = (float) state.getTime();
        xtc_write(xtcPath, (int) xtcAtoms.size(), 1, &frameStep, &time, positions.data(), box.data());
    }
    if (viewerFrame) {
        // Core 0x27's layout: one atom a line, %g, and nm, though viewerTop.json says "units": "A".
        ostringstream frame;
        frame << "[\n";
        char text[96];
        for (size_t i = 0; i < viewerAtoms.size(); i++) {
            const Vec3& p = x[viewerAtoms[i]];
            snprintf(text, sizeof(text), "%s[%g,%g,%g]", i ? ",\n" : "", p[0], p[1], p[2]);
            frame << text;
        }
        frame << "\n]";
        replaceFile(viewerFramePath(opt.dir, meta.viewerFrames), frame.str());
        meta.viewerFrames++;
    }
}

long long CoreRun::nextEvent(long long step) const {
    long long next = settings.steps;
    for (long long i : {progressInterval, settings.checkpoint, settings.xtc, settings.viewer, settings.globals})
        if (i > 0)
            next = min(next, (step/i+1)*i);
    return next;
}

double CoreRun::nsPerDay(long long steps, double seconds) const {
    return seconds > 0 ? steps*integrator->getStepSize()/seconds*86400/1000 : 0.0;
}

bool CoreRun::lifelineGone() const {
    return opt.lifeline > 0 && kill((pid_t) opt.lifeline, 0) != 0 && errno == ESRCH;
}

static bool due(long long step, long long interval) {
    return interval > 0 && step%interval == 0;
}

int CoreRun::stepToEnd() {
    startWatchdog();
    // From here a stop checkpoints first (the loop below sees the flag within about a second).
    deferSignals = true;
    long long startStep = context->getStepCount();
    if (interrupted) {
        say("interrupted at step %lld before stepping", startStep);
        return INTERRUPTED;
    }
    auto steppingStart = chrono::steady_clock::now(), lastCheckpointTime = steppingStart;
    long long lastCheckpointStep = startStep, chunk = 50;
    beat();
    watchdogArmed = true;
    while (context->getStepCount() < settings.steps) {
        long long step = context->getStepCount(), next = nextEvent(step);
        int stopCode = 0;
        string error;
        State state;
        try {
            while (step < next) {
                long long n = min(chunk, next-step);
                auto chunkStart = chrono::steady_clock::now();
                integrator->step((int) n);
                step += n;
                beat();
                double seconds = since(chunkStart);
                if (seconds < ChunkSecondsMin && n == chunk && chunk < ChunkStepsMax)
                    chunk *= 2;
                else if (seconds > ChunkSecondsMax && chunk > 1)
                    chunk /= 2;
                if (interrupted)
                    stopCode = INTERRUPTED;
                else if (lifelineGone())
                    stopCode = CLIENT_DIED;
                if (stopCode)
                    break;
            }
            state = context->getState(State::Energy | State::Positions);
            if (!isFinite(state))
                error = "Energy or a coordinate is not finite (energy "+to_string(state.getPotentialEnergy()+state.getKineticEnergy())+")";
        }
        catch (const OpenMMException& e) {
            error = e.what();
        }
        if (!error.empty())
            return badState(context->getStepCount(), error, "");
        step = context->getStepCount();
        // A stop can land on an output step. Its outputs come first, since the resumed run starts after it.
        if (step == next) {
            writeFrames(next, state, due(next, settings.xtc), due(next, settings.viewer), due(next, settings.globals));
            if (due(next, progressInterval) || next == settings.steps) {
                say("Completed %lld out of %lld steps (%lld%%)", next, settings.steps, next*100/settings.steps);
                writeProgress(opt, unit, settings, next);
            }
            bool timeUp = opt.checkpointMinutes > 0 && since(lastCheckpointTime) >= 60*opt.checkpointMinutes;
            if (due(next, settings.checkpoint) || next == settings.steps || timeUp) {
                if (next > lastCheckpointStep)
                    note("  Performance since last checkpoint: %.10g ns/day", nsPerDay(next-lastCheckpointStep, since(lastCheckpointTime)));
                if (next == settings.steps)
                    say("Average performance: %g ns/day", nsPerDay(next-startStep, since(steppingStart)));
                if (settings.checkpointTests) {
                    StateTestResult r;
                    if (!runStateTest(next, true, r))
                        return badState(next, r.error, r.detail);
                }
                saveCheckpoint(next);
                if (retries != 0) {
                    retries = 0;
                    writeCount(retriesPath, 0);
                }
                lastCheckpointTime = chrono::steady_clock::now();
                lastCheckpointStep = next;
                say("Checkpoint completed at step %lld", next);
                note("  energy %.6f kJ/mol, footprint %.1f MB", state.getPotentialEnergy()+state.getKineticEnergy(), footprintMB());
            }
        }
        if (stopCode && step < settings.steps) {
            if (step != lastCheckpointStep) {
                saveCheckpoint(step);
                say("Checkpoint completed at step %lld", step);
            }
            writeProgress(opt, unit, settings, step);
            if (stopCode == INTERRUPTED)
                say("interrupted at step %lld, checkpoint written", step);
            else
                say("lifeline process %ld is gone, checkpoint at step %lld", opt.lifeline, step);
            return stopCode;
        }
    }
    return FINISHED_UNIT;
}

// checkpointState.xml.bz2 holds what core 0x27's does (positions, velocities, forces, energies,
// parameters) and continues the unit's own step count, which this core counts from 0.
void CoreRun::writeFinalState() {
    note("Simulation completed successfully.");
    note("  Writing final state file for return: checkpointState.xml.bz2");
    context->setStepCount(meta.unitStep+context->getStepCount());
    State final = context->getState(State::Positions | State::Velocities | State::Forces | State::Energy | State::Parameters);
    ostringstream stateXml, integratorXml;
    XmlSerializer::serialize<State>(&final, "State", stateXml);
    XmlSerializer::serialize<Integrator>(integrator.get(), "Integrator", integratorXml);
    replaceFile(opt.dir+"/checkpointState.xml.bz2", bzip2(stateXml.str()));
    // The names and compression core 0x23 8.0.3 returns (fah-web-client-bastet#168 log, "Saving result file").
    replaceFile(opt.dir+"/checkpointIntegrator.xml", integratorXml.str());
    for (int i = 0; custom && i < custom->getNumGlobalVariables(); i++)
        note("global %s %.17g", custom->getGlobalVariableName(i).c_str(), custom->getGlobalVariable(i));
    note("done %lld steps, final energy %.6f kJ/mol, %.1f s in the core, footprint %.1f MB", context->getStepCount(),
        final.getPotentialEnergy()+final.getKineticEnergy(), since(processStart), footprintMB());
}

// wuresults_01.dat: a plain ustar tar of the result files, in the order core 0x27 saves them. Its own file
// is that tar behind a 512-byte header (version 1100, the unit's ids, payload size, two 20-byte digests and
// a 200-byte signature), which needs FAH's key; this core writes the tar alone. Written for a finished unit
// and, for one the core gave up on, with the logs and the state file, the files cores 0x23 and 0x24 return
// with BAD_WORK_UNIT, so the work server learns why.
static void writeResults(const Options& opt, const WorkUnit& unit, int code) {
    vector<string> names = {"logfile_"+opt.suffix+".txt"};
    if (code == BAD_WORK_UNIT)
        names.push_back(unit.stateFileName);  // present if the first test failed, or if the unit came as loose files
    if (code == FINISHED_UNIT)
        for (const char* name : {"checkpointIntegrator.xml", "checkpointState.xml.bz2", "positions.xtc"})
            names.push_back(name);
    names.push_back("science.log");
    if (code == FINISHED_UNIT) {
        names.push_back("xtcAtoms.csv.bz2");
        names.push_back(unit.globalsFileName);  // present only if core.xml asks for globals
    }
    vector<pair<string, string>> members;
    for (auto& name : names)
        if (!name.empty() && fileExists(opt.dir+"/"+name)) {
            say("Saving result file %s", name.c_str());
            members.push_back({name, ""});
        }
    // The shutdown line goes in before the tar, so the returned logs end with it.
    say("Folding@home Core Shutdown: %s", exitName(code));
    for (auto& member : members)
        member.second = readFile(opt.dir+"/"+member.first);
    replaceFile(opt.dir+"/wuresults_"+opt.suffix+".dat", writeTar(members));
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    Options opt = parseOptions(argc, argv);
    registerPlatforms();
    if (opt.info) {
        printf("minicore (fah-core-metal), OpenMM %s, platforms:", Platform::getOpenMMVersion().c_str());
        for (int i = 0; i < Platform::getNumPlatforms(); i++)
            printf(" %s", Platform::getPlatform(i).getName().c_str());
        printf("\n");
        return 0;
    }
    struct sigaction action = {};
    action.sa_handler = onSignal;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    WorkUnit unit;
    int code;
    try {
        openLogs(opt.dir, opt.suffix);
        code = CoreRun(opt, unit).run();
    }
    catch (const Failure& f) {
        say("ERROR:%s", f.what());
        code = f.code;
    }
    catch (const FileError& e) {
        say("ERROR:%s", e.what());
        code = FILE_IO_ERROR;
    }
    catch (const OpenMMException& e) {
        say("ERROR:OpenMM: %s", e.what());
        code = UNKNOWN_ERROR;
    }
    catch (const exception& e) {
        say("ERROR:%s", e.what());
        code = UNKNOWN_ERROR;
    }
    try {
        if (code == FINISHED_UNIT || code == BAD_WORK_UNIT)
            writeResults(opt, unit, code);
        else
            say("Folding@home Core Shutdown: %s", exitName(code));
    }
    catch (const exception& e) {
        say("ERROR:writing results: %s", e.what());
        say("Folding@home Core Shutdown: %s", exitName(FILE_IO_ERROR));
        return FILE_IO_ERROR;
    }
    return code;
}
