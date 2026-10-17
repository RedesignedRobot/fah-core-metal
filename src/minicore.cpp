// SPDX-License-Identifier: MIT
// minicore: runs one FAH work unit the way a GPU core does, on OpenMM's Metal platform on macOS (linked in)
// or its CUDA platform on Linux (a shared OpenMM and its plugins).
// Formats match core 0x27 8.2.1 as observed on project 18264 in October 2026; "core 0x27" below means
// that build. docs/PARITY.md lists each behaviour with its source; "S:N" cites line N of the core's strings.
//
// It takes the arguments fah-client passes a core (Unit.cpp:627-672 at fah-client-bastet 0ea081e, the same
// in master 706d6a0), picks the OpenMM platform from them, and passes the platform only the properties it
// declares. The work unit is <dir>/wudata_01.dat (a tar, or a bzip2 tar, behind FAH's 512-byte header or
// not), unpacked into <dir>/01 until a start writes every member, or the same files loose in <dir>. Each of
// system, integrator and state is found as core 0x27 finds it (.xml, .xml.gz or .xml.bz2; the .enc forms need
// FAH's key). core.xml sets the run with core 0x27's 26 options (S:19427-19472, alchemiscale-fah
// FahOpenMMCoreSettings): numSteps, checkpointFreq, xtcFreq, viewerFreq, globalVarFreq, trrFreq (a negative
// interval is a percentage), precision, xtcAtoms, xtcMinAtom, xtcMaxAtom, trrAtoms, trrMinAtom, trrMaxAtom,
// saveForcesInTrr, forceTolerance, energyTolerance, maxRetriesFromLastCheckpoint, disableCheckpointStateTests,
// disableViewer, MWExclusionThreshold, DisablePmeStream, globalVarFilename, minimize, minimizeTolerance,
// minimizeMaxIterations, minimizeReportFreq.
//
// The run, in core 0x27's order:
//   - PME alpha and grid pinned from the default box with CUDA's grid sizes, minimization if asked, then the
//     sanity checks against CPU: the unit's state.xml forces and energies, then the GPU state. A unit that
//     fails them, or whose state the Context refuses, goes back with BAD_WORK_UNIT at once;
//   - state tests are FAHBench's StateTests (NaN in positions, velocities or forces; |v| > 17.47 nm/ps; more
//     than half the velocity components exactly 0; |F| > 50000 kJ/mol/nm; RMS of the force magnitude
//     difference > forceTolerance; |dPE| or |dKE| > energyTolerance), run at step 0 and before every
//     checkpoint is written, so the one checkpoint on disk has always passed one;
//   - a non-finite energy or coordinate at an output step, an OpenMM exception while stepping, or a failed
//     test is a bad state: "An exception occurred at step N: ...", then "ERROR:98: Attempting to restart
//     from last good checkpoint by restarting core." and exit CORE_RESTART, which makes the client start the
//     core again (Unit::finalizeRun, retry()). The count lives in 01/numRetries and clears at each good
//     checkpoint; at maxRetriesFromLastCheckpoint the core gives up with BAD_WORK_UNIT and returns its state;
//   - 01/checkpoint is a createCheckpoint() blob behind a header that names the unit, written atomically with
//     a length and FNV-1a footer. A restarted core resumes from it and cuts positions.xtc, records.trr, the
//     globals file and the viewer frames back to it;
//   - positions.xtc (OpenMM's own xtc_utils writer), records.trr, xtcAtoms.csv.bz2, the globals CSV,
//     viewerTop.json and viewerFrameN.json in the layout fah-web-client-bastet's Visualization.vue reads, and
//     wuinfo_01.dat progress at every 1%;
//   - logfile_01.txt (the client copies it into its own log) and 01/science.log (every start of the unit, plus
//     the core's stdout and stderr) at core 0x27's two levels; -verbose adds minicore's own numbers;
//   - SIGINT or SIGTERM (the client's stop; it kills 60 s later) exits INTERRUPTED within about a second and
//     writes nothing more, a second signal warns and restores the default action, the death of -lifeline
//     exits CLIENT_DIED at once, and a watchdog exits WU_STALLED if stepping makes no progress for 600 s;
//   - at the end, one step past numSteps: checkpointIntegrator.xml, checkpointState.xml.bz2, positions.xtc,
//     records.trr, science.log, xtcAtoms.csv.bz2 and any globals file, in core 0x27's order, as a plain tar
//     in <dir>/wuresults_01.dat; then 01/ is removed.
//
// Platform choice, first match wins:
//   --platform NAME                    override, for Reference or CPU control runs
//   -metal-platform P -metal-device D  Metal, device D (fah-client-bastet#459 with cbang#215)
//   -gpu-vendor apple                  Metal, device -opencl-device or -gpu (what client master sends)
//   -gpu-platform cuda|opencl|hip      that platform, device -<api>-device or -gpu, if this build has it;
//                                      else BAD_ARGUMENTS (fah-client 8.5 sends cuda to core 0x27 on NVIDIA)
//   -gpu-vendor amd                    OpenCL, device -opencl-device or -gpu, in a build with MINICORE_OPENCL=1
//   -gpu-vendor nvidia                 CUDA, device -cuda-device or -gpu
// On OpenCL, chooseOpenCLDevice() checks the device and may fall back to another.
//
// Exit codes are the client's (ExitCode.h). Where the public record shows the real core's choice (98, 102,
// 114, 127) this follows it. The real wudata/wuresults containers carry FAH's signatures, which this core
// neither checks nor writes.

#include "OpenMM.h"
#include "host.h"
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
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <typeinfo>
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
static const time_t launchTime = time(NULL);
static const double StallSeconds = 600;
static const auto MonitorPeriod = chrono::milliseconds(500);
// Stepping goes in chunks sized to take between these, so a stop is seen within about a second.
static const double ChunkSecondsMin = 0.25, ChunkSecondsMax = 1;
static const long long ChunkStepsMax = 1 << 20;

struct Options {
    string exec, dir, suffix = "01", clientVersion, gpuPlatform, gpuVendor, platform;
    string args;                        // argv after the program name, each word followed by a space
    map<string, int> computeDevice;     // "opencl" -> -opencl-device, ...
    int gpu = -1;
    long lifeline = 0;
    double checkpointMinutes = 0;
    bool info = false, verbose = false;
};

// xtcAtoms or trrAtoms: "solute" (everything but water), a list of indices and ranges ("0-99,150"), or
// unset (all), then cut to [min, max].
struct AtomSelection {
    string atoms;
    long long min = -1, max = -1;
};

// The run as core.xml sets it. Intervals are in steps; 0 disables an output.
struct Settings {
    long long steps = 0, checkpoint = 0, xtc = 0, trr = 0, viewer = 0, globals = 0;
    string precision = "single", globalsFile = "globals.csv";
    AtomSelection xtcSelection, trrSelection;
    double forceTolerance = 5, energyTolerance = 10, mwExclusion = 0, minimizeTolerance = 10;
    int maxRetries = 2, minimizeIterations = 5000, minimizeReportFreq = 5;
    bool checkpointTests = true, disablePmeStream = true, minimize = false, saveForcesInTrr = false;
};

// Where the unit lives, and what writeResults needs to know about it.
struct WorkUnit {
    string workDir;          // <dir>/01: the core's own files, as core 0x27 keeps them
    string inputDir;         // workDir for a packed unit, <dir> for loose files
    optional<WuHeader> header;
    uint32_t coreType = 0x24;
    string globalsFile;      // core.xml globalVarFilename, once read
    string returnedState;    // the state file a BAD_WORK_UNIT result carries, if any
};

// Until stepping starts nothing needs saving, so a stop exits at once with stopLines. The client SIGKILLs a
// core about 60 s after SIGINT, and loading a large unit plus its first state test can take longer than that.
static atomic<bool> interrupted(false), deferSignals(false);
static atomic<int> signalCount(0);
static atomic<const char*> stopLines("ERROR:102: Core startup was interrupted by client.\n"
                                     "Folding@home Core Shutdown: INTERRUPTED\n");
// logfile_01.txt and science.log, as FILE* for logLine and as fds for the signal handler.
static FILE* logFile = NULL;
static FILE* scienceLog = NULL;
static int logFds[2] = {-1, -1};
// Core 0x27 writes "Exiting, please wait. . ." without a newline; the next line in each log supplies it.
static atomic<bool> pendingNewline[2];
static bool verbose = false;

// write(2) only: this runs in a signal handler, and a short write has nowhere to be reported. GCC ignores a
// void cast on warn_unused_result, hence the named result.
static void writeAll(const char* text) {
    for (int fd : logFds)
        if (fd >= 0) {
            ssize_t written = write(fd, text, strlen(text));
            (void) written;
        }
}

// cbang's ExitSignalHandler as core 0x27 runs it: the first signal asks the core to stop, the second warns
// and restores the default action so a third kills it (S:20423-20424, 20468; docs/PARITY.md S4).
static void onSignal(int number) {
    interrupted = true;
    writeAll(number == SIGINT ? "Caught signal SIGINT(2)\n" : "Caught signal SIGTERM(15)\n");
    if (++signalCount == 1) {
        writeAll("Exiting, please wait. . .");
        pendingNewline[0] = pendingNewline[1] = true;
    }
    else {
        writeAll("WARNING:Next signal will force exit\n\n");
        pendingNewline[0] = pendingNewline[1] = false;
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
    }
    if (!deferSignals) {
        writeAll("\n");
        writeAll(stopLines);
        _exit(INTERRUPTED);
    }
}

static string vformat(const char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    int size = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    string text(max(size, 0), '\0');
    vsnprintf(&text[0], text.size()+1, format, args);
    return text;
}

static string stringf(const char* format, ...) __attribute__((format(printf, 1, 2)));
static string stringf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    string text = vformat(format, args);
    va_end(args);
    return text;
}

// Core 0x27 keeps two levels. logfile_01.txt, which the client copies into its own log, has the banner,
// unit, intervals, platforms, context, progress, checkpoints, average speed, saved files, errors and the
// shutdown line; science.log has the banner and everything after the unit lines. say() writes both, tell()
// logfile_01.txt only, note() science.log only, and detail() science.log with -verbose. Before the logs
// open, every line goes to stderr.
enum { ClientLog = 1, ScienceLog = 2 };

static void logLine(int targets, const char* format, va_list args) {
    string line = vformat(format, args)+"\n";
    FILE* files[2] = {logFile, scienceLog};
    if (files[0] == NULL && files[1] == NULL) {
        fputs(line.c_str(), stderr);
        return;
    }
    for (int i = 0; i < 2; i++) {
        if (!(targets & (1 << i)) || files[i] == NULL)
            continue;
        if (pendingNewline[i].exchange(false))
            fputc('\n', files[i]);
        fputs(line.c_str(), files[i]);
        fflush(files[i]);
    }
}

static void say(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logLine(ClientLog | ScienceLog, format, args);
    va_end(args);
}

static void tell(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void tell(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logLine(ClientLog, format, args);
    va_end(args);
}

static void note(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void note(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logLine(ScienceLog, format, args);
    va_end(args);
}

static void detail(const char* format, ...) __attribute__((format(printf, 1, 2)));
static void detail(const char* format, ...) {
    if (!verbose)
        return;
    va_list args;
    va_start(args, format);
    logLine(ScienceLog, format, args);
    va_end(args);
}

static string isoTime(time_t t) {
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    return text;
}

static void openLogs(const Options& opt, const string& workDir) {
    if (mkdir(workDir.c_str(), 0755) != 0 && errno != EEXIST)
        throw FileError("cannot create "+workDir+": "+strerror(errno));
    logFile = fopen((opt.dir+"/logfile_"+opt.suffix+".txt").c_str(), "a");
    scienceLog = fopen((workDir+"/science.log").c_str(), "a");
    if (logFile == NULL || scienceLog == NULL)
        throw FileError("cannot open the log files in "+opt.dir+": "+strerror(errno));
    logFds[0] = fileno(logFile);
    logFds[1] = fileno(scienceLog);
    // Core 0x27 points stdout and stderr at science.log, so OpenMM's and the Metal compiler's own messages
    // reach the returned log (docs/PARITY.md A11; the client nulls both).
    if (dup2(logFds[1], STDOUT_FILENO) < 0 || dup2(logFds[1], STDERR_FILENO) < 0)
        throw FileError(string("cannot point stdout and stderr at science.log: ")+strerror(errno));
}

// Core 0x27 holds <dir>/wudata_01.lock, with its pid, for the whole run (S:19692-19694), so a second core
// on the unit stops. The descriptor stays open until exit.
static void lockUnit(const Options& opt) {
    string path = opt.dir+"/wudata_"+opt.suffix+".lock";
    int fd = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0)
        throw FileError("cannot open "+path+": "+strerror(errno));
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        note("Trying to acquire exclusive lock on unit");
        throw Failure(CORE_RESTART, "Failed to lock WU "+path);
    }
    string pid = to_string(getpid())+"\n";
    if (ftruncate(fd, 0) != 0 || write(fd, pid.data(), pid.size()) != (ssize_t) pid.size())
        throw FileError("cannot write "+path+": "+strerror(errno));
}

// cbang's lifeline and core 0x27's watchdog, polled from start-up. A dead client ends the core at once
// with CLIENT_DIED (S:20122-20126); stepping with no progress for StallSeconds ends it with WU_STALLED,
// which the client resumes from the last checkpoint. Neither writes a checkpoint, as core 0x27 doesn't.
static atomic<bool> watchdogArmed(false);
static atomic<double> lastBeat(0);

static double since(chrono::steady_clock::time_point start) {
    return chrono::duration<double>(chrono::steady_clock::now()-start).count();
}

static void beat() {
    lastBeat = since(processStart);
}

static void startMonitor(long lifeline) {
    thread([lifeline]() {
        while (true) {
            this_thread::sleep_for(MonitorPeriod);
            if (lifeline > 0 && kill((pid_t) lifeline, 0) != 0 && errno == ESRCH) {
                say("Lost lifeline PID %ld, exiting", lifeline);
                say("Folding@home Core Shutdown: %s", exitName(CLIENT_DIED));
                _exit(CLIENT_DIED);
            }
            if (watchdogArmed && since(processStart)-lastBeat > StallSeconds) {
                say("Watchdog triggered, requesting soft shutdown down");  // sic, core 0x27's wording
                say("Folding@home Core Shutdown: %s", exitName(WU_STALLED));
                _exit(WU_STALLED);
            }
        }
    }).detach();
}

// The core type from the executable's name, as the client installs cores (FahCore_27 for 0x27).
static optional<uint32_t> namedCoreType(const string& exec) {
    unsigned type;
    string base = exec.substr(exec.rfind('/')+1);
    if (sscanf(base.c_str(), "FahCore_%2x", &type) == 1)
        return type;
    return nullopt;
}

// cbang's info block, which core 0x27 opens both logs with: a title centred in a row of 80 stars, then
// "%11s: value" rows wrapped at 80 columns under the value.
static string starRow(const string& title) {
    string middle = title.empty() ? "" : " "+title+" ";
    size_t left = (80-middle.size())/2;
    return string(left, '*')+middle+string(80-middle.size()-left, '*');
}

static void infoRow(const char* key, const string& value) {
    string line = stringf("%11s:", key), word;
    istringstream words(value);
    for (bool first = true; words >> word; first = false) {
        if (!first && line.size()+1+word.size() > 80) {
            say("%s", line.c_str());
            line = string(12, ' ');
        }
        line += " "+word;
    }
    say("%s", line.c_str());
}

// Core 0x27's Core, System and OpenMM sections, with what minicore knows of itself. It has no libFAH or
// CBang to describe, and no build stamp.
static void writeBanner(const Options& opt) {
    say("%s", starRow("minicore (fah-core-metal)").c_str());
    infoRow("Core", "minicore");
    if (optional<uint32_t> type = namedCoreType(opt.exec))
        infoRow("Type", stringf("0x%02x", *type));
#ifdef __clang__
    infoRow("Compiler", "Clang " __clang_version__);
#else
    infoRow("Compiler", "GNU " __VERSION__);
#endif
    infoRow("Bits", to_string(8*sizeof(void*)));
    infoRow("Args", opt.args);
    say("%s", starRow("System").c_str());
    HostInfo machine = hostInfo(opt.exec);
    char host[256] = "", cwd[PATH_MAX] = "";
    gethostname(host, sizeof(host));
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        strcpy(cwd, "unknown");
    infoRow("CPU", machine.cpu);
    infoRow("CPUs", to_string(machine.cpus));
    infoRow("Memory", stringf("%.2fGiB", machine.memoryGiB));
    infoRow("OS Version", machine.os);
    infoRow("Hostname", host);
    infoRow("UTC Offset", to_string(localtime(&launchTime)->tm_gmtoff/3600));
    infoRow("PID", to_string(getpid()));
    infoRow("CWD", cwd);
    infoRow("Exec", machine.executable);
    say("%s", starRow("OpenMM").c_str());
    infoRow("Version", Platform::getOpenMMVersion());
    say("%s", string(80, '*').c_str());
}

// The OpenMM platform and device the client's arguments ask for, and why.
struct PlatformChoice {
    string platform, reason;
    string deviceArg;  // the argument that named the device, for core 0x27's "  opencl-device 0 specified"
    int device = 0;
};

static PlatformChoice choosePlatform(const Options& opt) {
    static const map<string, string> PlatformNames = {{"cuda", "CUDA"}, {"opencl", "OpenCL"}, {"hip", "HIP"}};
    PlatformChoice c;
    auto deviceFrom = [&](const string& api) {
        if (opt.computeDevice.count(api)) {
            c.device = opt.computeDevice.at(api);
            c.deviceArg = api+"-device";
        }
        else if (opt.gpu >= 0) {
            c.device = opt.gpu;
            c.deviceArg = "gpu";
        }
    };
    if (!opt.platform.empty()) {
        c.platform = opt.platform;
        c.reason = "--platform";
    }
    else if (opt.computeDevice.count("metal")) {
        c.platform = "Metal";
        deviceFrom("metal");
        c.reason = "client passed -metal-platform/-metal-device";
    }
    else if (opt.gpuVendor == "apple") {
        c.platform = "Metal";
        deviceFrom("opencl");
        c.reason = "client passed -gpu-vendor apple";
    }
    else if (PlatformNames.count(opt.gpuPlatform)) {
        c.platform = PlatformNames.at(opt.gpuPlatform);
        deviceFrom(opt.gpuPlatform);
        c.reason = "client passed -gpu-platform "+opt.gpuPlatform+" -gpu-vendor "+opt.gpuVendor;
    }
#ifdef MINICORE_OPENCL
    else if (opt.gpuVendor == "amd") {
        c.platform = "OpenCL";
        deviceFrom("opencl");
        c.reason = "client passed -gpu-vendor amd";
    }
#endif
    else if (opt.gpuVendor == "nvidia") {
        c.platform = "CUDA";
        deviceFrom("cuda");
        c.reason = "client passed -gpu-vendor nvidia";
    }
    else
        throw Failure(BAD_ARGUMENTS, "Core requires at least one of 'cuda-device' or 'opencl-device' to be "
                      "specified and not disabled.");  // S:19510
    return c;
}

// Core 0x27's nine "GPU info" lines, after its version line and again after "Using ...".
static void logGpuInfo(const PlatformChoice& choice) {
    GpuInfo gpu = gpuInfo(choice.platform, choice.device);
    say("  GPU info: Platform: %s", choice.platform.c_str());
    say("  GPU info: PlatformIndex: 0");
    say("  GPU info: Device: %s", gpu.device.c_str());
    say("  GPU info: DeviceIndex: %d", choice.device);
    say("  GPU info: Vendor: %s", gpu.vendor.c_str());
    say("  GPU info: PCI: %s", gpu.pci.c_str());
    say("  GPU info: Compute: %s", gpu.compute.c_str());
    say("  GPU info: Driver: %s", gpu.driver.c_str());
    say("  GPU info: GPU: %s", choice.platform != "Reference" && choice.platform != "CPU" ? "true" : "false");
}

#ifdef MINICORE_OPENCL
static string openclDeviceLine(int index, const OpenCLDevice& d) {
    return stringf("OpenCL device %d: %s, vendor %s, %s, fp64 %s, %d compute units, max work group %ld, max work item %ld, %s, "
                   "driver %s", index, d.name.c_str(), d.vendor.c_str(), d.gpu ? "GPU" : "CPU", d.fp64 ? "yes" : "no",
                   d.computeUnits, d.maxWorkGroup, d.maxWorkItem, d.version.c_str(), d.driver.c_str());
}

// fah-client numbers OpenCL devices over CL_DEVICE_TYPE_ALL (cbang's OpenCLLibrary.cpp) and sends that index as
// -opencl-device. On a Mac, which has one platform and no accelerators, that is OpenMM's DeviceIndex, so it is
// passed through. OpenMM's OpenCL platform skips a device without cl_khr_fp64 in mixed and double precision, or
// one with work groups under 32 threads (OpenCLContext.cpp). The device the client names must be able to run the
// unit, or the core exits GPU_UNAVAILABLE_ERROR rather than take another GPU, which would be another slot's.
// A device that isn't a GPU runs only when -opencl-device names it: OpenMM uses Apple's CPU device only then
// (patch 0005), since it skips that device on its own, and patch 0006 sizes its work groups. If the device is missing, or is not a GPU and only -gpu
// named it, the core falls back to the first GPU that can run the unit, with a WARNING line, and otherwise exits
// GPU_UNAVAILABLE_ERROR, as the Metal path does on a Mac with no usable GPU.
static void chooseOpenCLDevice(PlatformChoice& choice, const string& precision) {
    vector<OpenCLDevice> devices = openclDevices();
    int count = devices.size();
    say("OpenCL platform 0, %d devices:", count);
    for (int i = 0; i < count; i++)
        say("  %s", openclDeviceLine(i, devices[i]).c_str());
    auto problem = [&](int i) -> string {
        if (precision != "single" && !devices[i].fp64)
            return "does not report cl_khr_fp64, which "+precision+" precision needs";
        if (devices[i].maxWorkGroup < 32 || devices[i].maxWorkItem < 32)
            return "takes work groups under 32 threads, which OpenMM needs";
        return "";
    };
    int asked = choice.device;
    bool named = choice.deviceArg == "opencl-device";
    if (asked >= 0 && asked < count && (devices[asked].gpu || named)) {
        const OpenCLDevice& d = devices[asked];
        string why = problem(asked);
        if (!why.empty())
            throw Failure(GPU_UNAVAILABLE_ERROR, stringf("OpenCL device %d, %s, ", asked, d.name.c_str())+why);
        if (d.gpu)
            say("  Using OpenCL device %d, as the client asked", asked);
        else
            say("WARNING: OpenCL device %d, %s, is not a GPU; using it because -opencl-device names it", asked,
                d.name.c_str());
        return;
    }
    string why = asked >= 0 && asked < count ? stringf("OpenCL device %d is not a GPU", asked) :
                                               stringf("there is no OpenCL device %d", asked);
    for (int i = 0; i < count; i++)
        if (devices[i].gpu && problem(i).empty()) {
            choice.device = i;
            say("WARNING: %s; falling back to OpenCL device %d, %s", why.c_str(), i, devices[i].name.c_str());
            return;
        }
    throw Failure(GPU_UNAVAILABLE_ERROR, why+", and no OpenCL GPU can run "+precision+" precision");
}
#endif

// Core 0x27 looks for each input under these names in turn and takes the first ("Checking for state.xml.enc"
// ... "Found state.xml.bz2", S:19325-19333).
static const char* const InputSuffixes[] = {".xml.enc", ".xml.gz.enc", ".xml.bz2.enc", ".xml", ".xml.gz", ".xml.bz2"};
static const char* const Inputs[] = {"integrator", "state", "system"};

static string findInput(const string& dir, const string& base) {
    for (const char* suffix : InputSuffixes) {
        string name = base+suffix;
        note("    Checking for %s", name.c_str());
        if (fileExists(dir+"/"+name)) {
            note("    Found %s", name.c_str());
            return name;
        }
    }
    throw Failure(MISSING_WORK_FILES, "Cannot find "+base+"{.xml.enc|.xml.gz.enc|.xml.bz2.enc|.xml|.xml.gz|.xml.bz2}");  // S:19330-19333
}

static bool endsWith(const string& text, const string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size()-suffix.size(), suffix.size(), suffix) == 0;
}

static string unpackInput(const string& path) {
    if (endsWith(path, ".enc"))
        throw Failure(BAD_WORK_UNIT, path+" is encrypted with FAH's key, which this core does not have");
    string data = readFile(path);
    try {
        if (endsWith(path, ".gz"))
            return gunzip(data, path);
        if (endsWith(path, ".bz2"))
            return bunzip2(data, path);
        return data;
    }
    catch (const DataError& e) {
        throw Failure(BAD_WORK_UNIT, e.what());
    }
}

// Core 0x27 extracts the tar while 01/checkpt.crc is missing and creates checkpt.crc after the last member,
// so a start stopped partway through extracts again (docs/PARITY.md I12). minicore has no checkpt.crc
// (PARITY K3), and this empty file takes its place.
static const char* const UnpackedFile = "unpacked";

// The unit: <dir>/wudata_01.dat, unpacked into <dir>/01 as core 0x27 does, or its files loose in <dir>. A
// unit from a FAH work server has a 512-byte header before the tar, and core 0x27 logs
// "Project: P (Run R, Clone C, Gen G)" from it on every start, then "Digital signatures verified". This core
// reads the plain fields only: checking the digests and signature needs FAH's key.
static void readWorkUnit(const Options& opt, WorkUnit& unit) {
    string wudata = opt.dir+"/wudata_"+opt.suffix+".dat";
    if (!fileExists(wudata)) {
        unit.inputDir = opt.dir;
        return;
    }
    unit.inputDir = unit.workDir;
    try {
        string archive = readFile(wudata);
        unit.header = readWuHeader(archive);
        if (unit.header) {
            const WuHeader& h = *unit.header;
            tell("Project: %u (Run %u, Clone %u, Gen %u)", h.project, h.run, h.clone, h.gen);
            if (h.payloadBytes != archive.size()-512)
                throw Failure(BAD_WORK_UNIT, "wudata header says "+to_string(h.payloadBytes)+" payload bytes, the file has "+
                              to_string(archive.size()-512));
            archive.erase(0, 512);
        }
        string unpacked = unit.workDir+"/"+UnpackedFile;
        if (!fileExists(unpacked)) {
            if (isBzip2(archive))
                archive = bunzip2(archive, wudata);
            vector<pair<string, string>> members = readTar(archive);
            if (members.empty())
                throw Failure(BAD_WORK_UNIT, wudata+" holds no files");
            for (auto& member : members) {
                string name = member.first.substr(member.first.rfind('/')+1);
                if (name.empty() || name[0] == '.')
                    continue;  // directories, and the AppleDouble files macOS tar adds
                tell("Reading tar file %s", name.c_str());
                replaceFile(unit.workDir+"/"+name, member.second);
            }
            replaceFile(unpacked, "");
        }
        if (unit.header)
            tell("Digital signatures not checked: version %u header for core 0x%02x, signature scheme private to FAH",
                 unit.header->version, unit.header->coreType);
    }
    catch (const DataError& e) {
        throw Failure(BAD_WORK_UNIT, e.what());
    }
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

static AtomSelection configSelection(const map<string, string>& config, const string& prefix) {
    AtomSelection s;
    s.atoms = configText(config, prefix+"Atoms", "");
    s.min = (long long) configNumber(config, prefix+"MinAtom", -1);
    s.max = (long long) configNumber(config, prefix+"MaxAtom", -1);
    return s;
}

// The globals file shares 01/ with the core's own files and must not be one of them.
static bool isCoreFileName(const string& name) {
    static const char* const prefixes[] = {"wudata_", "wuresults_", "wuinfo_", "logfile_", "checkpoint",
        "viewerFrame", "viewerTop", "core.xml", "system.xml", "integrator.xml", "state.xml", "positions.xtc",
        "records.trr", "science.log", "xtcAtoms.csv", "stepsDone", "numRetries", UnpackedFile};
    for (const char* prefix : prefixes)
        if (name.compare(0, strlen(prefix), prefix) == 0)
            return true;
    return false;
}

static Settings readSettings(const WorkUnit& unit) {
    string path = unit.inputDir+"/core.xml";
    if (!fileExists(path))
        throw Failure(MISSING_WORK_FILES, "Cannot find core.xml");
    map<string, string> config;
    try {
        config = parseCoreXml(readFile(path));
    }
    catch (const DataError& e) {
        throw Failure(BAD_WORK_UNIT, e.what());
    }
    static const set<string> known = {"numSteps", "checkpointFreq", "xtcFreq", "viewerFreq", "globalVarFreq",
        "globalVarFilename", "precision", "xtcAtoms", "xtcMinAtom", "xtcMaxAtom", "forceTolerance", "energyTolerance",
        "maxRetriesFromLastCheckpoint", "disableCheckpointStateTests", "disableViewer", "MWExclusionThreshold",
        "DisablePmeStream", "minimize", "minimizeTolerance", "minimizeMaxIterations", "minimizeReportFreq", "trrFreq",
        "trrMinAtom", "trrMaxAtom", "trrAtoms", "saveForcesInTrr"};
    for (auto& entry : config)
        if (known.count(entry.first) == 0)
            detail("core.xml: ignoring unknown option %s", entry.first.c_str());
    Settings s;
    s.steps = (long long) configNumber(config, "numSteps", 0);
    if (s.steps <= 0)
        throw Failure(BAD_WORK_UNIT, "core.xml has no numSteps");
    s.checkpoint = interval(config, "checkpointFreq", -5, s.steps);  // 0 disables checkpointing (S:19456)
    s.xtc = interval(config, "xtcFreq", 0, s.steps);
    s.trr = interval(config, "trrFreq", 0, s.steps);
    s.viewer = interval(config, "viewerFreq", -1, s.steps);
    if (configNumber(config, "disableViewer", 0) != 0)
        s.viewer = 0;
    s.globals = interval(config, "globalVarFreq", 0, s.steps);
    s.globalsFile = configText(config, "globalVarFilename", s.globalsFile);
    if (s.globalsFile.empty() || s.globalsFile[0] == '.' || s.globalsFile.find('/') != string::npos ||
        isCoreFileName(s.globalsFile))
        throw Failure(BAD_WORK_UNIT, "core.xml: globalVarFilename "+s.globalsFile+" is not a file name of its own");
    s.precision = configText(config, "precision", s.precision);
    if (s.precision != "single" && s.precision != "mixed" && s.precision != "double")
        throw Failure(BAD_WORK_UNIT, "core.xml: precision "+s.precision+" is not single, mixed or double");
    s.xtcSelection = configSelection(config, "xtc");
    s.trrSelection = configSelection(config, "trr");
    s.saveForcesInTrr = configNumber(config, "saveForcesInTrr", 0) != 0;
    s.forceTolerance = configNumber(config, "forceTolerance", s.forceTolerance);
    s.energyTolerance = configNumber(config, "energyTolerance", s.energyTolerance);
    s.maxRetries = (int) configNumber(config, "maxRetriesFromLastCheckpoint", s.maxRetries);
    s.checkpointTests = configNumber(config, "disableCheckpointStateTests", 0) == 0;
    s.mwExclusion = configNumber(config, "MWExclusionThreshold", s.mwExclusion);
    s.disablePmeStream = configNumber(config, "DisablePmeStream", s.disablePmeStream) != 0;
    s.minimize = configNumber(config, "minimize", s.minimize) != 0;
    s.minimizeTolerance = configNumber(config, "minimizeTolerance", s.minimizeTolerance);
    s.minimizeIterations = (int) configNumber(config, "minimizeMaxIterations", s.minimizeIterations);
    s.minimizeReportFreq = (int) configNumber(config, "minimizeReportFreq", s.minimizeReportFreq);
    return s;
}

static string describeInterval(long long interval, long long steps) {
    if (interval <= 0)
        return "disabled";
    return stringf("%lld steps (%g%%) [%lld total]", interval, 100.0*interval/steps, steps/interval);
}

// wuinfo_01.dat as core 0x27 writes it: 512 bytes, u32 type, the name "Core27 p18264" at 4, u32 total 100
// at 84, u32 done in percent at 88, u32 steps per percent at 92, the rest zero. The client reads
// {type; 80 reserved bytes; total; done} and drops the progress of a core whose type isn't the one FAH
// assigned the unit to (Unit.cpp:709-727).
static void writeProgress(const Options& opt, const WorkUnit& unit, const Settings& s, long long done) {
    char info[512] = {};
    uint32_t type = unit.coreType, total = 100, percent = (uint32_t) (min(done, s.steps)*100/s.steps),
             perPercent = (uint32_t) max(1LL, s.steps/100);
    memcpy(info, &type, 4);
    snprintf(info+4, 80, "Core%02x p%u", type, unit.header ? unit.header->project : 0);
    memcpy(info+84, &total, 4);
    memcpy(info+88, &percent, 4);
    memcpy(info+92, &perPercent, 4);
    replaceFile(opt.dir+"/wuinfo_"+opt.suffix+".dat", string(info, sizeof(info)));
}

static void usage(const string& why) {
    fprintf(stderr, "minicore: %s\n"
            "usage: minicore -dir DIR [client arguments] [-verbose] [--platform NAME]\n"
            "       minicore --info\n"
            "client arguments (fah-client 8.5): -dir DIR -suffix S -version V -lifeline PID -gpu-uuid U\n"
            "    -gpu-platform cuda|opencl -gpu-vendor TYPE -<opencl|cuda|hip|metal>-platform N\n"
            "    -<opencl|cuda|hip|metal>-device N -gpu N -np N -checkpoint MINUTES\n"
            "-verbose adds minicore's own numbers to science.log\n"
            "--platform NAME runs on that OpenMM platform (Reference, CPU) instead of the GPU\n"
            "--info prints the OpenMM version and platforms\n", why.c_str());
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
    return arg.size() > suffix.size()+1 && arg[0] == '-' && arg[1] != '-' && endsWith(arg, suffix);
}

static Options parseOptions(int argc, char** argv) {
    static const set<string> valued = {"-dir", "-suffix", "-version", "-lifeline", "-gpu-platform", "-gpu-vendor",
        "-gpu", "-checkpoint", "-gpu-uuid", "-np", "--platform"};
    Options opt;
    opt.exec = argv[0];
    for (int i = 1; i < argc; i++)
        opt.args += string(argv[i])+" ";
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--info" || arg == "-info") {
            opt.info = true;
            continue;
        }
        if (arg == "-verbose") {
            opt.verbose = true;
            continue;
        }
        if (!valued.count(arg) && !isApiArg(arg, "-device") && !isApiArg(arg, "-platform")) {
            // cbang's warning (S:20359), on stderr before the logs exist, which the client nulls. Core 0x27
            // ran under client 8.5.6 with -gpu-uuid and -cuda-platform, neither in its strings, and went on.
            fprintf(stderr, "Ignoring invalid or unsupported argument '%s'\n", arg.c_str());
            if (i+1 < argc && argv[i+1][0] != '-')
                i++;
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
        else if (arg == "--platform") opt.platform = value;
        else if (isApiArg(arg, "-device")) opt.computeDevice[arg.substr(1, arg.size()-8)] = parseInt(arg, value);
        // -gpu-uuid, -np and -<api>-platform: accepted, unused
    }
    if (!opt.info && opt.dir.empty())
        usage("-dir is required");
    return opt;
}

// The property map core 0x27 passes, in its order, reduced to the names the platform declares: Context
// creation throws "Illegal property name" on any other (ContextImpl.cpp:100-101). Metal declares Precision
// only; CUDA declares all four.
static map<string, string> chooseProperties(const Settings& s, const PlatformChoice& choice, Platform& platform) {
    const vector<string>& names = platform.getPropertyNames();
    auto offered = [&](const char* name) {
        if (find(names.begin(), names.end(), name) != names.end())
            return true;
        detail("    property %s not offered by %s, not passed", name, platform.getName().c_str());
        return false;
    };
    map<string, string> properties;
    if (offered("DeviceIndex")) {
        properties["DeviceIndex"] = to_string(choice.device);
        note("    Setting %s platform DeviceIndex to %d", platform.getName().c_str(), choice.device);
    }
    else if (choice.device != 0 && platform.getName() == "Metal")
        throw Failure(GPU_UNAVAILABLE_ERROR, "the Metal platform has no DeviceIndex property and uses the system GPU; "
                      "the client asked for device "+to_string(choice.device));
    if (offered("UseBlockingSync")) {
        properties["UseBlockingSync"] = "false";
        note("    Disabling UseBlockingSync on %s platform", platform.getName().c_str());
    }
    if (offered("Precision")) {
        properties["Precision"] = s.precision;
        note("    Setting platform precision to %s", s.precision.c_str());
    }
    if (s.disablePmeStream && offered("DisablePmeStream")) {
        properties["DisablePmeStream"] = "true";
        note("    Setting DisablePmeStream to 1");
    }
    return properties;
}

// Core 0x27 sets PME alpha and grid itself before any Context exists ("Ewald parameters: alpha 2.62826 nx
// 189 ny 180 nz 160" on P18264): OpenMM's estimate from the default box (NonbondedForceImpl::
// calcPMEParameters), with each size rounded up to one with no prime factor above 7, as CUDA's
// findLegalDimension does. Pinned, the Metal context and the CPU reference use the grid core 0x27's CUDA
// context used; Metal would round 187 x 176 x 152 to 13-smooth sizes instead.
static int sevenSmooth(int n) {
    for (;; n++) {
        int rest = n;
        for (int factor : {2, 3, 5, 7})
            while (rest%factor == 0)
                rest /= factor;
        if (rest == 1)
            return n;
    }
}

static void pinPme(System& system) {
    for (int i = 0; i < system.getNumForces(); i++) {
        NonbondedForce* nonbonded = dynamic_cast<NonbondedForce*>(&system.getForce(i));
        if (nonbonded == NULL || (nonbonded->getNonbondedMethod() != NonbondedForce::PME &&
                                  nonbonded->getNonbondedMethod() != NonbondedForce::LJPME))
            continue;
        double tolerance = nonbonded->getEwaldErrorTolerance(), alpha;
        int size[3];
        nonbonded->getPMEParameters(alpha, size[0], size[1], size[2]);
        if (alpha == 0) {
            Vec3 box[3];
            system.getDefaultPeriodicBoxVectors(box[0], box[1], box[2]);
            alpha = sqrt(-log(2*tolerance))/nonbonded->getCutoffDistance();
            for (int j = 0; j < 3; j++)
                size[j] = sevenSmooth(max(6, (int) ceil(2*alpha*box[j][j]/(3*pow(tolerance, 0.2)))));
            nonbonded->setPMEParameters(alpha, size[0], size[1], size[2]);
        }
        note("    Ewald error tolerance in force %d is %g", i, tolerance);
        note("    Ewald parameters: alpha %g nx %d ny %d nz %d", alpha, size[0], size[1], size[2]);
    }
}

// Core 0x27's CoreMinReporter (S:19606, 19615): a line every minimizeReportFreq iterations.
class CoreMinReporter : public MinimizationReporter {
public:
    explicit CoreMinReporter(int frequency) : frequency(frequency) {}
    bool report(int iteration, const vector<double>& x, const vector<double>& grad, map<string, double>& args) override {
        if (frequency > 0 && iteration%frequency == 0)
            note("  Minimization: iteration: %d System energy: %g Restraint energy %g Max constraint error: %g", iteration,
                 args["system energy"], args["restraint energy"], args["max constraint error"]);
        return false;
    }

private:
    int frequency;
};

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
    va_list args;
    va_start(args, format);
    r.error = vformat(format, args);
    va_end(args);
    return r;
}

// FAHBench's StateTests (fahbench/StateTests.cpp), run against a CPU Context of the same pinned System,
// with its own integrator.
class StateTester {
public:
    StateTester(unique_ptr<System> system, unique_ptr<Integrator> integrator, const Settings& settings,
                const string& platformName)
        : settings(settings), refSystem(std::move(system)), refIntegrator(std::move(integrator)),
          refContext(new Context(*refSystem, *refIntegrator, Platform::getPlatformByName(platformName))) {}

    // The unit's state.xml carries the forces and energies its last core computed (every unit seen so far);
    // core 0x27 compares both against CPU. Empty if state.xml has no energies.
    optional<StateTestResult> compareInitial(const State& given) {
        if (!(given.getDataTypes() & State::Energy))
            return nullopt;
        StateTestResult r;
        r.givenPE = given.getPotentialEnergy();
        r.givenKE = given.getKineticEnergy();
        refContext->setState(given);
        State expected = refContext->getState(State::Forces | State::Energy);
        r.referencePE = expected.getPotentialEnergy();
        r.referenceKE = expected.getKineticEnergy();
        if (given.getDataTypes() & State::Forces)  // without forces, the energies alone
            compareForces(given.getForces(), expected.getForces(), r);
        checkEnergies(r);
        return r;
    }

    StateTestResult test(Context& gpuContext, const Integrator& integrator, bool zeroVelocityTest) {
        StateTestResult r;
        State gpu = gpuContext.getState(State::Positions | State::Velocities | State::Forces | State::Energy | State::Parameters);
        const vector<Vec3>& x = gpu.getPositions();
        const vector<Vec3>& v = gpu.getVelocities();
        const vector<Vec3>& f = gpu.getForces();
        // FAHBench checkForNans, in core 0x27's words (S:19642-19645)
        const pair<const vector<Vec3>*, const char*> arrays[] = {{&x, "positions"}, {&v, "velocities"}, {&f, "forces"}};
        for (auto& array : arrays)
            for (const Vec3& value : *array.first)
                if (std::isnan(value[0]) || std::isnan(value[1]) || std::isnan(value[2]))
                    return fail(r, "NaNs detected in %s.", array.second);
        // FAHBench checkForDiscrepancies; the index pair is core 0x23's wording
        size_t zeroVelocities = 0;
        for (size_t i = 0; i < v.size(); i++)
            for (int j = 0; j < 3; j++) {
                if (fabs(v[i][j]) > MaxVelocity)
                    return fail(r, "Discrepancy: Velocities are blowing up! %zu %d", i, j);
                if (v[i][j] == 0)
                    zeroVelocities++;
            }
        if (zeroVelocityTest && zeroVelocities > 3*v.size()/2)
            return fail(r, "Discrepancy: More than half of all velocities set exactly to zero!");
        for (size_t i = 0; i < f.size(); i++)
            for (int j = 0; j < 3; j++)
                if (fabs(f[i][j]) > MaxForce)
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
        compareForces(f, expected.getForces(), r);
        r.referencePE = expected.getPotentialEnergy();
        r.givenPE = gpu.getPotentialEnergy();
        r.referenceKE = expected.getKineticEnergy();
        r.givenKE = gpu.getKineticEnergy();
        r.compared = true;
        checkEnergies(r);
        return r;
    }

private:
    // RMS over atoms of the difference in force magnitude, FAHBench's measure, plus max |dF| and the RMS of
    // the force vector difference for the record.
    void compareForces(const vector<Vec3>& f, const vector<Vec3>& g, StateTestResult& r) const {
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
        if (!(r.magnitudeRmse <= settings.forceTolerance))
            fail(r, "Force RMSE error of %g with threshold of %g", r.magnitudeRmse, settings.forceTolerance);
    }

    // !(error <= limit), so a NaN error fails too. An earlier failure stays the one reported.
    void checkEnergies(StateTestResult& r) const {
        r.dPE = fabs(r.referencePE-r.givenPE);
        r.dKE = fabs(r.referenceKE-r.givenKE);
        if (!r.ok())
            return;
        if (!(r.dPE <= settings.energyTolerance)) {
            fail(r, "Potential energy error of %g, threshold of %g", r.dPE, settings.energyTolerance);
            r.detail = stringf("Reference Potential Energy: %g | Given Potential Energy: %g", r.referencePE, r.givenPE);
        }
        else if (!(r.dKE <= settings.energyTolerance)) {
            fail(r, "Kinetic energy error of %g, threshold of %g", r.dKE, settings.energyTolerance);
            r.detail = stringf("Reference Kinetic Energy: %g | Given Kinetic Energy: %g", r.referenceKE, r.givenKE);
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

static vector<int> selectAtoms(const AtomSelection& selection, const string& key, const vector<char>& water) {
    long n = (long) water.size();
    vector<char> keep(n, 1);
    if (selection.atoms == "solute") {
        for (long i = 0; i < n; i++)
            keep[i] = !water[i];
    }
    else if (!selection.atoms.empty()) {
        fill(keep.begin(), keep.end(), 0);
        stringstream list(selection.atoms);
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
                throw Failure(BAD_WORK_UNIT, "core.xml: bad "+key+" entry "+item);
            for (long i = a; i <= b; i++)
                keep[i] = 1;
        }
    }
    vector<int> atoms;
    for (long i = 0; i < n; i++)
        if (keep[i] && (selection.min < 0 || i >= selection.min) && (selection.max < 0 || i <= selection.max))
            atoms.push_back((int) i);
    return atoms;
}

// Masses core 0x27 names in viewerTop.json. On P18264 it names C 12.01, O 16.00 and Cl 35.45 and leaves
// N 14.01, Na 22.99 and every repartitioned mass as "?". Its own table is private; this one, matched within
// 1e-4 amu, reproduces all 2520 atoms of that unit.
static const struct { const char* symbol; int number; double mass; } StandardMasses[] = {
    {"H", 1, 1.008}, {"C", 6, 12.01}, {"N", 7, 14.007}, {"O", 8, 16.00}, {"F", 9, 18.998}, {"Na", 11, 22.98977},
    {"P", 15, 30.974}, {"S", 16, 32.06}, {"Cl", 17, 35.45}, {"K", 19, 39.098}};

// viewerTop.json for fah-web-client-bastet's Visualization.vue, byte for byte in core 0x27's layout:
// units "A", atoms as [name, 0, 0, 0, number] one a line, then bonds as [a,b] over the shown atoms. An atom
// with a standard mass carries its symbol and atomic number, any other reads ["?",0,0,0,trunc(mass)]. The
// bonds are the HarmonicBondForce bonds and constraints between shown atoms, sorted and without repeats.
static void writeViewerTopology(const string& path, const System& system, const vector<int>& shown) {
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
    vector<int> index(system.getNumParticles(), -1);
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
}

// What a checkpoint records besides the OpenMM blob: the FNV-1a hash of the unit's state.xml, so a
// checkpoint is never applied to another unit; the unit's step count from state.xml; and the sizes of the
// append-only outputs at that step, so a restarted core can cut off what the dead process wrote after it.
struct CheckpointMeta {
    unsigned long long unitId = 0;
    long long step = 0, unitStep = 0, xtcBytes = 0, trrBytes = 0, globalsBytes = 0;
    int viewerFrames = 0;
};

static const char CheckpointFormat[] =
    "minicore-checkpoint 4 unit=%llx step=%lld unitstep=%lld xtc=%lld trr=%lld globals=%lld viewer=%d\n";

static string checkpointHeader(const CheckpointMeta& meta) {
    return stringf(CheckpointFormat, meta.unitId, meta.step, meta.unitStep, meta.xtcBytes, meta.trrBytes,
                   meta.globalsBytes, meta.viewerFrames);
}

// Reads the header line off body, which leaves the OpenMM checkpoint.
static bool unpackCheckpoint(string& body, CheckpointMeta& meta) {
    size_t end = body.find('\n');
    if (end == string::npos || end > 255)
        return false;
    string line = body.substr(0, end+1);
    if (sscanf(line.c_str(), CheckpointFormat, &meta.unitId, &meta.step, &meta.unitStep, &meta.xtcBytes,
               &meta.trrBytes, &meta.globalsBytes, &meta.viewerFrames) != 7)
        return false;
    body.erase(0, end+1);
    return true;
}

static void truncateTo(const string& path, long long bytes) {
    long long size = fileSize(path);
    if (size > bytes) {
        if (truncate(path.c_str(), bytes) != 0)
            throw FileError("cannot truncate "+path+": "+strerror(errno));
        note("File is longer than at last checksum, truncating: %s", path.c_str());  // S:19664
    }
    else if (size >= 0 && size < bytes)
        detail("%s has %lld bytes, the checkpoint recorded %lld", path.c_str(), size, bytes);
}

static int readCount(const string& path) {
    ifstream in(path);
    int n = 0;
    in >> n;
    return in ? n : 0;
}

// A short write throws, so a full disk ends the run instead of thinning the data.
static void appendToFile(const string& path, const string& data) {
    ofstream out(path, ios::app | ios::binary);
    out << data;
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

static vector<float> pick(const vector<Vec3>& values, const vector<int>& atoms) {
    vector<float> picked(3*atoms.size());
    for (size_t i = 0; i < atoms.size(); i++)
        for (int j = 0; j < 3; j++)
            picked[3*i+j] = (float) values[atoms[i]][j];
    return picked;
}

static vector<float> boxOf(const State& state) {
    Vec3 a, b, c;
    state.getPeriodicBoxVectors(a, b, c);
    return {(float) a[0], (float) a[1], (float) a[2], (float) b[0], (float) b[1], (float) b[2],
            (float) c[0], (float) c[1], (float) c[2]};
}

// One GROMACS TRR frame in XDR's big-endian words (xdrfile_trr.c do_trnheader), the format of core 0x27's
// records.trr ("GMX_trn_file", S:19653): the box, positions, and forces if saveForcesInTrr is set.
static string trrFrame(long long step, float time, const vector<float>& box, const vector<float>& x, const vector<float>& f) {
    string out;
    auto word = [&out](uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            out += (char) (value >> shift);
    };
    auto real = [&word](float value) {
        uint32_t bits;
        memcpy(&bits, &value, 4);
        word(bits);
    };
    int natoms = (int) (x.size()/3);
    word(1993);  // magic
    word(13);    // strlen("GMX_trn_file")+1
    word(12);
    out += "GMX_trn_file";
    // ir, e, box, vir, pres, top, sym, x, v and f sizes in bytes, then natoms, step and nre
    for (int field : {0, 0, 36, 0, 0, 0, 0, 12*natoms, 0, 4*(int) f.size(), natoms, (int) step, 0})
        word((uint32_t) field);
    real(time);
    real(0);  // lambda
    for (const vector<float>* values : {&box, &x, &f})
        for (float value : *values)
            real(value);
    return out;
}

static bool due(long long step, long long interval) {
    return interval > 0 && step%interval == 0;
}

#ifdef __APPLE__
// link.sh links OpenMM and its CPU and Metal platforms in, the way a shipped core does; there are no plugins.
extern "C" void registerCpuPlatform();
extern "C" void registerCpuPmeKernelFactories();
extern "C" void registerMetalPlatform();
#ifdef MINICORE_OPENCL
extern "C" void registerOpenCLPlatform();
#endif
#endif

// The CPU platform runs the state tests, as core 0x27 does ("Using CPU platform for reference calculations.").
static const char TestPlatform[] = "CPU";

static void registerPlatforms() {
#ifdef __APPLE__
    // The CPU PME kernels register before Metal exists, so only Reference and CPU get them.
    registerCpuPlatform();
    registerCpuPmeKernelFactories();
    registerMetalPlatform();
#ifdef MINICORE_OPENCL
    registerOpenCLPlatform();
#endif
#else
    // A shared OpenMM, as build-linux.sh links it, has its plugins in lib/plugins beside libOpenMM. OpenMM itself
    // looks in OPENMM_PLUGIN_DIR or /usr/local/openmm/lib/plugins; conda's Python sets the variable, a core can't.
    string dir = Platform::getDefaultPluginsDirectory();
    Dl_info lib;
    if (getenv("OPENMM_PLUGIN_DIR") == NULL && dladdr((void*) &Platform::getNumPlatforms, &lib) != 0) {
        dir = lib.dli_fname;
        dir = dir.substr(0, dir.rfind('/'))+"/plugins";
    }
    Platform::loadPluginsFromDirectory(dir);
#endif
}

// An input file as found and unpacked.
struct InputFile {
    string name, text;
};

template <class T>
static unique_ptr<T> parse(const InputFile& file) {
    stringstream in(file.text);
    try {
        return unique_ptr<T>(XmlSerializer::deserialize<T>(in));
    }
    catch (const exception& e) {
        throw Failure(BAD_WORK_UNIT, "deserializing "+file.name+": "+e.what());
    }
}

// One process's run of a unit: start or resume, step, checkpoint, finish.
class CoreRun {
public:
    CoreRun(const Options& opt, WorkUnit& unit)
        : opt(opt), unit(unit), checkpointPath(unit.workDir+"/checkpoint"), stepsDonePath(unit.workDir+"/stepsDone"),
          retriesPath(unit.workDir+"/numRetries"), xtcPath(unit.workDir+"/positions.xtc"),
          trrPath(unit.workDir+"/records.trr") {}
    int run();

private:
    void logSettings();
    InputFile readInput(const string& base);
    void createContexts();
    int checkStartState();
    void prepareOutputs();
    bool resume();
    void startOutputs();
    int stepToEnd();
    int writeOutputs(long long step, const State& state, bool checkpointNow);
    void writeFinalState();
    int rejectUnit(const string& error, const string& detail);
    int badState(long long step, const string& error, const string& detail);
    void saveCheckpoint(long long step);
    void appendXtcFrame(long long step, const State& state);
    void appendTrrFrame(long long step, const State& state);
    void appendGlobalsRow(long long step, const State& state);
    void writeViewerFrame(const State& state, int frame);
    long long nextEvent(long long step) const;
    double nsPerDay(long long steps, double seconds) const;

    const Options& opt;
    WorkUnit& unit;
    const string checkpointPath, stepsDonePath, retriesPath, xtcPath, trrPath;
    string globalsPath, integratorXml;
    PlatformChoice choice;
    Settings settings;
    long long progressInterval = 1, lastCheckpointStep = 0;
    time_t lastCheckpointTime = 0;
    unique_ptr<System> system;
    unique_ptr<Integrator> integrator;
    CustomIntegrator* custom = NULL;
    unique_ptr<State> initialState;  // the unit's state.xml, until the start is settled
    unique_ptr<Context> context;
    unique_ptr<StateTester> tester;
    CheckpointMeta meta;
    vector<int> xtcAtoms, trrAtoms, viewerAtoms, globalOrder;
    int retries = 0;
};

int CoreRun::run() {
    tell("%s", starRow("Log Started "+isoTime(launchTime)).c_str());
    writeBanner(opt);
    readWorkUnit(opt, unit);
    optional<uint32_t> named = namedCoreType(opt.exec);
    if (named && unit.header && *named != unit.header->coreType)
        throw Failure(UNKNOWN_ERROR, stringf("Core, unit mismatch.  Core type is %02x unit wants %02x", *named,
                                             unit.header->coreType));  // S:19698
    unit.coreType = named ? *named : unit.header ? unit.header->coreType : 0x24;
    choice = choosePlatform(opt);
#ifdef MINICORE_OPENCL
    if (choice.platform == "OpenCL")
        chooseOpenCLDevice(choice, readSettings(unit).precision);
#endif
    say("minicore (fah-core-metal), OpenMM %s", Platform::getOpenMMVersion().c_str());
    logGpuInfo(choice);
    settings = readSettings(unit);
    unit.globalsFile = settings.globalsFile;
    globalsPath = unit.workDir+"/"+settings.globalsFile;
    progressInterval = max(1LL, settings.steps/100);
    logSettings();

    createContexts();
    if (int code = checkStartState())
        return code;
    note("  Core Contexts initialized.");
    note("[ Initialized Core Contexts... ]");
    prepareOutputs();
    say("  Using %s on %s Platform and gpu %d", choice.platform.c_str(), choice.platform.c_str(), choice.device);
    logGpuInfo(choice);
    note("  Creating a list of all Context parameters:");
    for (auto& param : context->getParameters())
        note("    %s", param.first.c_str());
    bool resumed = resume();
    initialState.reset();
    note("%s", "");
    note("  v(^_^)v  MD ready starting from step %lld", context->getStepCount());
    note("%s", "");
    note("  Checking for existence of retries file (numRetries) ...");
    retries = readCount(retriesPath);
    if (retries > 0)
        note("  There have been %d retries already.", retries);
    note("  Starting watchdog...");
    beat();
    watchdogArmed = true;
    lastCheckpointStep = context->getStepCount();
    lastCheckpointTime = time(NULL);
    detail("start-up %.1f s since exec, footprint %.0f MB", since(processStart), memoryMB());
    if (!resumed) {
        startOutputs();
        if (int code = writeOutputs(0, context->getState(State::Energy | State::Positions), true))
            return code;
    }

    int code = stepToEnd();
    watchdogArmed = false;
    if (code != FINISHED_UNIT)
        return code;
    // Past the final checkpoint a stop exits at once with the shutdown line alone: the next start resumes at
    // numSteps and writes the results again, well inside the client's 60 s.
    stopLines = "Folding@home Core Shutdown: INTERRUPTED\n";
    deferSignals = false;
    if (interrupted)
        return INTERRUPTED;
    writeFinalState();
    return FINISHED_UNIT;
}

// Core 0x27's Init and Main blocks, up to the input files.
void CoreRun::logSettings() {
    note("[ Entering Init ]");
    note("  Launch time: %s", isoTime(launchTime).c_str());
    note("  Arguments passed: %s", opt.args.c_str());
    note("  For testState comparison of CPU and GPU, will use:");
    note("    forceTolerance: %g kJ/mol/nm", settings.forceTolerance);
    note("    energyTolerance: %g kJ/mol", settings.energyTolerance);
    note("[ Leaving  Init ]");
    note("[ Entering Main ]");
    note("  Reading core settings...");
    note("  Total number of steps: %lld", settings.steps);
    say("  Checkpoint write interval: %s", describeInterval(settings.checkpoint, settings.steps).c_str());
    say("  JSON viewer frame write interval: %s", describeInterval(settings.viewer, settings.steps).c_str());
    say("  XTC frame write interval: %s", describeInterval(settings.xtc, settings.steps).c_str());
    say("  TRR frame write interval: %s", describeInterval(settings.trr, settings.steps).c_str());
    say("  Global context and integrator variables write interval: %s", describeInterval(settings.globals, settings.steps).c_str());
    if (!settings.checkpointTests)
        note("  Disabling checkpoint state tests.");
    detail("  Precision %s, max retries from last checkpoint %d, core type 0x%02x, platform %s device %d: %s",
           settings.precision.c_str(), settings.maxRetries, unit.coreType, choice.platform.c_str(), choice.device,
           choice.reason.c_str());
    for (const char* base : Inputs)
        findInput(unit.inputDir, base);
}

InputFile CoreRun::readInput(const string& base) {
    note("  Finding %s XML file...", base.c_str());
    InputFile file;
    file.name = findInput(unit.inputDir, base);
    note("  Loading %s from %s", base.c_str(), file.name.c_str());
    file.text = unpackInput(unit.inputDir+"/"+file.name);
    return file;
}

// Core 0x27's context set-up: the platforms, the inputs (integrator.xml for the GPU and for the reference),
// the pinned PME parameters, integrator.xml once more, then the GPU and reference Contexts.
void CoreRun::createContexts() {
    note("[ Initializing Core Contexts ]");
    say("There are %d platforms available.", Platform::getNumPlatforms());
    for (int i = 0; i < Platform::getNumPlatforms(); i++) {
        const string& name = Platform::getPlatform(i).getName();
        say("Platform %d: %s", i, name.c_str());
        if (name == choice.platform && !choice.deviceArg.empty())
            say("  %s %d specified", choice.deviceArg.c_str(), choice.device);
    }
    Platform* platform;
    try {
        platform = &Platform::getPlatformByName(choice.platform);
    }
    catch (const OpenMMException&) {
        throw Failure(BAD_ARGUMENTS, choice.platform+" is not in this core ("+choice.reason+")");
    }

    integrator = parse<Integrator>(readInput("integrator"));
    custom = dynamic_cast<CustomIntegrator*>(integrator.get());
    unique_ptr<Integrator> refIntegrator = parse<Integrator>(readInput("integrator"));
    system = parse<System>(readInput("system"));
    note("  Found %d atoms, %d forces.", system->getNumParticles(), system->getNumForces());
    InputFile stateFile = readInput("state");
    meta.unitId = fnv1a(stateFile.text.data(), stateFile.text.size());
    unit.returnedState = unit.inputDir+"/"+stateFile.name;  // what a failed start returns
    initialState = parse<State>(stateFile);
    stateFile = InputFile();
    pinPme(*system);
    const Integrator& integratorRef = *integrator;
    note("    Integrator Type: %s", typeid(integratorRef).name());
    note("    Constraint Tolerance: %g", integrator->getConstraintTolerance());
    note("    Time Step in PS: %g", integrator->getStepSize());
    integratorXml = readInput("integrator").text;  // core 0x27's third read; these bytes go back as checkpointIntegrator.xml
    unique_ptr<System> refSystem(XmlSerializer::clone<System>(*system));

    say("Attempting to create %s context:", choice.platform.c_str());
    say("  Configuring platform %s", choice.platform.c_str());
    string problem = choice.platform == "Metal" ? metalDeviceProblem() : "";
    if (!problem.empty())
        throw Failure(GPU_UNAVAILABLE_ERROR, problem);
    map<string, string> properties = chooseProperties(settings, choice, *platform);
    try {
        context.reset(new Context(*system, *integrator, *platform, properties));
    }
    catch (const OpenMMException& e) {
        throw Failure(GPU_INITIALIZATION_ERROR, string("creating the Context: ")+e.what());
    }
    note("  Configuring CPU/Reference platform for sanity checks...");
    note("    Using %s platform for reference calculations.", TestPlatform);
    tester = make_unique<StateTester>(std::move(refSystem), std::move(refIntegrator), settings, TestPlatform);
}

// The unit's state on the GPU, minimized if core.xml asks, then core 0x27's sanity checks. A unit that fails
// them is not retried (cores 0x23 and 0x24, forum t=42176).
int CoreRun::checkStartState() {
    try {
        context->setState(*initialState);
    }
    catch (const OpenMMException& e) {
        return rejectUnit(e.what(), "");
    }
    meta.unitStep = initialState->getStepCount();
    context->setStepCount(0);
    if (settings.minimize) {
        note("  Minimizing the system before starting the simulation...");
        note("    Maximum number of minimization iterations: %d", settings.minimizeIterations);
        note("    Minimization report frequency: %d", settings.minimizeReportFreq);
        detail("    Minimization tolerance: %g kJ/mol/nm", settings.minimizeTolerance);
        CoreMinReporter reporter(settings.minimizeReportFreq);
        LocalEnergyMinimizer::minimize(*context, settings.minimizeTolerance, settings.minimizeIterations, &reporter);
        note("  Minimization complete");
    }
    // The reference platform refuses a NaN coordinate outright, so its exception rejects the unit too.
    try {
        note("  Performing initial sanity checks before starting work...");
        note("  Comparing forces and energies between initial State and %s...", TestPlatform);
        optional<StateTestResult> initial = tester->compareInitial(*initialState);
        if (initial) {
            detail("    initial State: force RMSE %.4g kJ/mol/nm, |dPE| %.4g, |dKE| %.4g kJ/mol", initial->magnitudeRmse,
                   initial->dPE, initial->dKE);
            if (!initial->ok())
                return rejectUnit(initial->error, initial->detail);
        }
        else
            detail("    state.xml has no energies");
        note("  Initial sanity checks complete.");
        note("  Comparing forces and energies between GPU and %s...", TestPlatform);
        StateTestResult r = tester->test(*context, *integrator, false);
        detail("    GPU: force RMSE %.4g kJ/mol/nm, |dPE| %.4g, |dKE| %.4g kJ/mol", r.magnitudeRmse, r.dPE, r.dKE);
        if (!r.ok())
            return rejectUnit(r.error, r.detail);
    }
    catch (const OpenMMException& e) {
        return rejectUnit(e.what(), "");
    }
    return 0;
}

// The atom lists core 0x27 logs, xtcAtoms.csv.bz2, viewerTop.json and the globals order.
void CoreRun::prepareOutputs() {
    int n = system->getNumParticles();
    vector<char> water(n, 0);
    vector<double> moleculeMass(n, 0);
    for (auto& molecule : context->getMolecules()) {
        double total = 0;
        for (int i : molecule)
            total += system->getParticleMass(i);
        bool inWater = isWater(*system, molecule);
        for (int i : molecule) {
            water[i] = inWater;
            moleculeMass[i] = total;
        }
    }
    long waterAtoms = count(water.begin(), water.end(), 1);
    long light = count_if(moleculeMass.begin(), moleculeMass.end(), [&](double m) { return m < settings.mwExclusion; });
    note("[ Finding non-water atoms ]");
    note("  %ld non-water atoms found / %d particles", n-waterAtoms, n);
    note("  %ld water atoms found / %d particles", waterAtoms, n);
    note("[ Finished finiding non-water atoms ]");  // sic
    note("[ Checking molecular weights ]");
    note("  %ld atoms above %g Da", n-light, settings.mwExclusion);
    note("  %ld atoms below %g Da", light, settings.mwExclusion);
    note("[ Finished checking molecular weights ]");
    // What the viewer shows: every massive particle outside water, in a molecule at or above
    // MWExclusionThreshold.
    vector<int> keep;
    for (int i = 0; i < n; i++)
        if (!water[i] && moleculeMass[i] >= settings.mwExclusion && system->getParticleMass(i) > 0)
            keep.push_back(i);
    note("  KeepAtoms has %zu atoms", keep.size());
    if (settings.xtc > 0) {
        xtcAtoms = selectAtoms(settings.xtcSelection, "xtcAtoms", water);
        note("  %zu atoms specified to write to XTC files", xtcAtoms.size());
        ostringstream list;
        for (size_t i = 0; i < xtcAtoms.size(); i++)
            list << (i ? "," : "") << xtcAtoms[i];
        replaceFile(unit.workDir+"/xtcAtoms.csv.bz2", bzip2(list.str()));
    }
    if (settings.trr > 0) {
        trrAtoms = selectAtoms(settings.trrSelection, "trrAtoms", water);
        detail("  %zu atoms specified to write to TRR files", trrAtoms.size());
    }
    if (settings.viewer > 0) {
        viewerAtoms = keep;
        note("[ Writing viewer topology ]");
        writeViewerTopology(opt.dir+"/viewerTop.json", *system, viewerAtoms);
        note("  %zu atoms written", viewerAtoms.size());
        note("[ Viewer topology written ]");
    }
    for (int i = 0; custom && i < custom->getNumGlobalVariables(); i++)
        globalOrder.push_back(i);
    sort(globalOrder.begin(), globalOrder.end(), [this](int a, int b) {
        return custom->getGlobalVariableName(a) < custom->getGlobalVariableName(b);
    });
}

bool CoreRun::resume() {
    if (!fileExists(checkpointPath))
        return false;
    note("  Binary checkpoint file found. Restoring context from checkpoint...");
    string body = readVerified(checkpointPath), problem;
    CheckpointMeta saved;
    if (body.empty() || !unpackCheckpoint(body, saved))
        problem = checkpointPath+" failed its length, hash or header check";
    else if (saved.unitId != meta.unitId)
        problem = checkpointPath+" belongs to another work unit";
    else {
        istringstream in(body);
        try {
            context->loadCheckpoint(in);
        }
        catch (const OpenMMException& e) {
            problem = e.what();
            context->setState(*initialState);
            context->setStepCount(0);
        }
    }
    if (!problem.empty()) {
        note("  Could not resume from checkpoint: %s", problem.c_str());
        return false;
    }
    meta = saved;
    note("  Binary checkpoint read. Resuming from step %lld", context->getStepCount());
    truncateTo(xtcPath, meta.xtcBytes);
    truncateTo(trrPath, meta.trrBytes);
    truncateTo(globalsPath, meta.globalsBytes);
    removeViewerFrames(opt.dir, meta.viewerFrames);
    return true;
}

// A fresh start drops what an earlier start wrote, and opens the globals file with its header: Step, Time,
// the Context parameters and the integrator globals by name, and volume (alchemiscale-fah
// tests/data/globals.csv; fah-xchem reads Step, PersesAtFoldingAtHome reads Time).
void CoreRun::startOutputs() {
    for (const string& path : {xtcPath, trrPath, globalsPath})
        unlink(path.c_str());
    removeViewerFrames(opt.dir, 0);
    if (settings.globals <= 0)
        return;
    string header = "Step,Time";
    for (auto& param : context->getParameters())
        header += ","+param.first;
    for (int i : globalOrder)
        header += ","+custom->getGlobalVariableName(i);
    appendToFile(globalsPath, header+",volume\n");
}

int CoreRun::stepToEnd() {
    // From here a stop exits between chunks, through main, with the shutdown line alone (docs/PARITY.md S3:
    // no checkpoint, no progress, 102 within a second).
    deferSignals = true;
    long long chunk = 50;
    while (context->getStepCount() < settings.steps) {
        long long step = context->getStepCount(), next = nextEvent(step);
        string error;
        State state;
        try {
            while (step < next && !interrupted) {
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
            }
            if (interrupted)
                return INTERRUPTED;
            state = context->getState(State::Energy | State::Positions);
            if (!isFinite(state))
                error = "Energy or a coordinate is not finite (energy "+to_string(state.getPotentialEnergy()+state.getKineticEnergy())+")";
        }
        catch (const OpenMMException& e) {
            error = e.what();
        }
        if (!error.empty())
            return badState(context->getStepCount(), error, "");
        bool timeUp = opt.checkpointMinutes > 0 && difftime(time(NULL), lastCheckpointTime) >= 60*opt.checkpointMinutes;
        if (int code = writeOutputs(next, state, due(next, settings.checkpoint) || next == settings.steps || timeUp))
            return code;
    }
    return FINISHED_UNIT;
}

// One output step in core 0x27's order: progress, then at a checkpoint the speed and the state test, the
// xtc, trr and globals frames, the checkpoint, and the viewer frame last, except viewerFrame0, which comes
// first. Neither trajectory has a step-0 frame. The checkpoint counts the viewer frame it precedes. Returns
// a bad state's exit code, else 0.
int CoreRun::writeOutputs(long long step, const State& state, bool checkpointNow) {
    bool viewerFrame = due(step, settings.viewer);
    int frame = meta.viewerFrames;
    if (viewerFrame)
        meta.viewerFrames++;
    if (viewerFrame && step == 0)
        writeViewerFrame(state, frame);
    if (due(step, progressInterval) || step == settings.steps) {
        writeProgress(opt, unit, settings, step);
        say("Completed %lld out of %lld steps (%lld%%)", step, settings.steps, step*100/settings.steps);
    }
    if (checkpointNow) {
        // Whole seconds, as core 0x27 counts them; its average is the last interval's figure (22.42990654
        // and 22.4299 ns/day on P18264).
        double performance = nsPerDay(step-lastCheckpointStep, difftime(time(NULL), lastCheckpointTime));
        if (step > lastCheckpointStep)
            note("  Performance since last checkpoint: %.10g ns/day", performance);
        if (step == settings.steps)
            say("Average performance: %g ns/day", performance);
        if (settings.checkpointTests) {
            note("  Running tests");
            StateTestResult r = tester->test(*context, *integrator, step > 0);
            if (r.compared)
                detail("    max|dF| %.4g, rms dF %.4g, force RMSE %.4g kJ/mol/nm (limit %g), |dPE| %.4g, |dKE| %.4g kJ/mol (limit %g)",
                       r.maxForceError, r.rmsForceError, r.magnitudeRmse, settings.forceTolerance, r.dPE, r.dKE,
                       settings.energyTolerance);
            if (!r.ok())
                return badState(step, r.error, r.detail);
            note("  All tests passed.");
        }
    }
    if (step > 0 && due(step, settings.xtc))
        appendXtcFrame(step, state);
    if (step > 0 && due(step, settings.trr))
        appendTrrFrame(step, state);
    if (due(step, settings.globals))
        appendGlobalsRow(step, state);
    if (checkpointNow) {
        note("  Writing binary checkpoint");
        saveCheckpoint(step);
        note("  Binary checkpoint complete. Cleared numRetries file.");
        say("Checkpoint completed at step %lld", step);
        detail("  energy %.6f kJ/mol, footprint %.1f MB", state.getPotentialEnergy()+state.getKineticEnergy(), memoryMB());
        lastCheckpointStep = step;
        lastCheckpointTime = time(NULL);
    }
    if (viewerFrame && step > 0)
        writeViewerFrame(state, frame);
    return 0;
}

// A unit that fails at the start: cores 0x23 and 0x24 log the error with an ERROR: prefix, return the
// logs and the unit's state file under its own name, and exit BAD_WORK_UNIT (forum t=42176: "ERROR:Potential
// energy error of 296.63, threshold of 20" then "Saving result file state.xml.bz2").
int CoreRun::rejectUnit(const string& error, const string& detail) {
    say("ERROR:%s", error.c_str());
    if (!detail.empty())
        say("ERROR:%s", detail.c_str());
    return BAD_WORK_UNIT;
}

// A bad state: log it as the live core does, then ask the client for a restart or give up. At the limit,
// core 0x22 0.0.10's lines, which core 0x27 carries (forum p=338624, S:19600-19601), and the last state
// goes back as state.xml.
int CoreRun::badState(long long step, const string& error, const string& detail) {
    say("An exception occurred at step %lld: %s", step, error.c_str());
    if (!detail.empty())
        say("%s", detail.c_str());
    replaceFile(retriesPath, to_string(++retries));
    note("  Updating retries file (numRetries) with %d retries.", retries);
    if (retries < settings.maxRetries) {
        say("ERROR:98: Attempting to restart from last good checkpoint by restarting core.");
        return CORE_RESTART;
    }
    say("Max number of attempts to resume from last checkpoint (%d) reached. Aborting.", settings.maxRetries);
    say("ERROR:114: Max number of attempts to resume from last checkpoint reached.");
    unit.returnedState.clear();
    try {
        State last = context->getState(State::Positions | State::Velocities | State::Parameters);
        ostringstream xml;
        XmlSerializer::serialize<State>(&last, "State", xml);
        unit.returnedState = unit.workDir+"/state.xml";
        replaceFile(unit.returnedState, xml.str());
    }
    catch (const OpenMMException& e) {
        say("ERROR:no state to return: %s", e.what());
    }
    return BAD_WORK_UNIT;
}

// 01/checkpoint, then core 0x27's stepsDone and numRetries beside it (docs/PARITY.md K2).
void CoreRun::saveCheckpoint(long long step) {
    meta.step = step;
    meta.xtcBytes = max(0LL, fileSize(xtcPath));
    meta.trrBytes = max(0LL, fileSize(trrPath));
    meta.globalsBytes = max(0LL, fileSize(globalsPath));
    ostringstream data;
    data << checkpointHeader(meta);
    context->createCheckpoint(data);
    writeAtomically(checkpointPath, data.str());
    replaceFile(stepsDonePath, to_string(step));
    retries = 0;
    replaceFile(retriesPath, "0");
}

// Unwrapped, as core 0x27 writes positions.xtc and the viewer frames. xtc_write throws on a file it cannot
// open but not on a failed write, hence the size check.
void CoreRun::appendXtcFrame(long long step, const State& state) {
    note("  Appending to XTC file positions.xtc");
    vector<float> positions = pick(state.getPositions(), xtcAtoms), box = boxOf(state);
    int frameStep = (int) step;
    float time = (float) state.getTime();
    long long before = max(0LL, fileSize(xtcPath));
    try {
        xtc_write(xtcPath, (int) xtcAtoms.size(), 1, &frameStep, &time, positions.data(), box.data());
    }
    catch (const runtime_error& e) {
        throw FileError(xtcPath+": "+e.what());
    }
    if (fileSize(xtcPath) <= before)
        throw FileError("cannot append a frame to "+xtcPath);
}

void CoreRun::appendTrrFrame(long long step, const State& state) {
    note("  Appending to TRR file records.trr");
    vector<float> forces;
    if (settings.saveForcesInTrr)
        forces = pick(context->getState(State::Forces).getForces(), trrAtoms);
    appendToFile(trrPath, trrFrame(step, (float) state.getTime(), boxOf(state), pick(state.getPositions(), trrAtoms), forces));
}

void CoreRun::appendGlobalsRow(long long step, const State& state) {
    ostringstream row;
    row.precision(15);
    row << step << "," << state.getTime();
    for (auto& param : context->getParameters())
        row << "," << param.second;
    for (int i : globalOrder)
        row << "," << custom->getGlobalVariable(i);
    row << "," << state.getPeriodicBoxVolume() << "\n";
    appendToFile(globalsPath, row.str());
}

// Core 0x27's layout: one atom a line, %g, and nm, though viewerTop.json says "units": "A".
void CoreRun::writeViewerFrame(const State& state, int frame) {
    const vector<Vec3>& x = state.getPositions();
    ostringstream out;
    out << "[\n";
    for (size_t i = 0; i < viewerAtoms.size(); i++) {
        const Vec3& p = x[viewerAtoms[i]];
        out << stringf("%s[%g,%g,%g]", i ? ",\n" : "", p[0], p[1], p[2]);
    }
    out << "\n]";
    replaceFile(viewerFramePath(opt.dir, frame), out.str());
}

long long CoreRun::nextEvent(long long step) const {
    long long next = settings.steps;
    for (long long i : {progressInterval, settings.checkpoint, settings.xtc, settings.trr, settings.viewer, settings.globals})
        if (i > 0)
            next = min(next, (step/i+1)*i);
    return next;
}

double CoreRun::nsPerDay(long long steps, double seconds) const {
    return seconds > 0 ? steps*integrator->getStepSize()/seconds*86400/1000 : 0.0;
}

// checkpointState.xml.bz2 holds what core 0x27's does (positions, velocities, forces, energies, parameters),
// one step past numSteps as core 0x27 returns it (P18264: input + 1,250,001 in every gen), and continues the
// unit's own step count, which this core counts from 0. checkpointIntegrator.xml is the unit's integrator.xml.
void CoreRun::writeFinalState() {
    note("Simulation completed successfully.");
    note("Encryption is disabled.");
    note("  Writing final state file for return: checkpointState.xml.bz2");
    integrator->step(1);
    context->setStepCount(meta.unitStep+context->getStepCount());
    State final = context->getState(State::Positions | State::Velocities | State::Forces | State::Energy | State::Parameters);
    ostringstream stateXml;
    XmlSerializer::serialize<State>(&final, "State", stateXml);
    replaceFile(unit.workDir+"/checkpointState.xml.bz2", bzip2(stateXml.str()));
    replaceFile(unit.workDir+"/checkpointIntegrator.xml", integratorXml);
    note("  Done.");
    note("%s", isoTime(time(NULL)).c_str());
    note("[ Leaving Main ]");
    for (int i = 0; custom && i < custom->getNumGlobalVariables(); i++)
        detail("global %s %.17g", custom->getGlobalVariableName(i).c_str(), custom->getGlobalVariable(i));
    detail("done, final energy %.6f kJ/mol, %.1f s in the core, footprint %.1f MB",
           final.getPotentialEnergy()+final.getKineticEnergy(), since(processStart), memoryMB());
}

// wuresults_01.dat: a plain ustar tar of the result files, in the order core 0x27 saves them. Its own file
// is that tar behind a 512-byte header (version 1100, the unit's ids, payload size, two 20-byte digests and
// a 200-byte signature), which needs FAH's key; this core writes the tar alone. Core 0x27 runs in 01/, so it
// names the logfile ../logfile_01.txt, and reads each member right after its line, so science.log ends at
// its own. A unit given up on returns the logs, any globals file and the state, the files cores 0x22 to 0x24
// return with BAD_WORK_UNIT, so the work server learns why.
static void writeResults(const Options& opt, const WorkUnit& unit, int code) {
    vector<pair<string, string>> files = {{"logfile_"+opt.suffix+".txt", opt.dir+"/logfile_"+opt.suffix+".txt"}};
    auto add = [&](const string& name) {
        files.push_back({name, unit.workDir+"/"+name});
    };
    if (code == FINISHED_UNIT)
        for (const char* name : {"checkpointIntegrator.xml", "checkpointState.xml.bz2", "positions.xtc", "records.trr"})
            add(name);
    if (code == BAD_WORK_UNIT && !unit.globalsFile.empty())
        add(unit.globalsFile);
    add("science.log");
    if (code == FINISHED_UNIT) {
        add("xtcAtoms.csv.bz2");
        add(unit.globalsFile);
    }
    if (code == BAD_WORK_UNIT && !unit.returnedState.empty())
        files.push_back({unit.returnedState.substr(unit.returnedState.rfind('/')+1), unit.returnedState});
    vector<pair<string, string>> members;
    for (auto& file : files) {
        if (!fileExists(file.second))
            continue;
        bool inWorkDir = file.second.compare(0, unit.workDir.size()+1, unit.workDir+"/") == 0;
        say("Saving result file %s%s", inWorkDir ? "" : "../", file.first.c_str());
        members.push_back({file.first, readFile(file.second)});
    }
    replaceFile(opt.dir+"/wuresults_"+opt.suffix+".dat", writeTar(members));
}

// Core 0x27 removes 01/ once a finished unit's results are written. science.log goes with it, so the
// shutdown line that follows lands in logfile_01.txt alone.
static void removeWorkDir(const string& workDir) {
    fclose(scienceLog);
    scienceLog = NULL;
    logFds[1] = -1;
    vector<string> names;
    if (DIR* dir = opendir(workDir.c_str())) {
        while (dirent* entry = readdir(dir))
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                names.push_back(entry->d_name);
        closedir(dir);
    }
    for (const string& name : names)
        if (unlink((workDir+"/"+name).c_str()) != 0)
            say("cannot remove %s/%s: %s", workDir.c_str(), name.c_str(), strerror(errno));
    if (rmdir(workDir.c_str()) != 0)
        say("cannot remove %s: %s", workDir.c_str(), strerror(errno));
}

int main(int argc, char** argv) {
    // First, as core 0x27 has them 6 ms after exec (docs/PARITY.md S1).
    struct sigaction action = {};
    action.sa_handler = onSignal;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    setvbuf(stdout, NULL, _IOLBF, 0);
    Options opt = parseOptions(argc, argv);
    verbose = opt.verbose;
    registerPlatforms();
    if (opt.info) {
        printf("minicore (fah-core-metal), OpenMM %s, platforms:", Platform::getOpenMMVersion().c_str());
        for (int i = 0; i < Platform::getNumPlatforms(); i++)
            printf(" %s", Platform::getPlatform(i).getName().c_str());
        printf("\n");
#ifdef MINICORE_OPENCL
        vector<OpenCLDevice> devices = openclDevices();
        for (int i = 0; i < (int) devices.size(); i++)
            printf("%s\n", openclDeviceLine(i, devices[i]).c_str());
#endif
        return 0;
    }
    WorkUnit unit;
    unit.workDir = opt.dir+"/"+opt.suffix;
    int code;
    try {
        openLogs(opt, unit.workDir);
        lockUnit(opt);
        startMonitor(opt.lifeline);
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
        if (code == FINISHED_UNIT) {
            // The results are out. A stop now would send the client back to a unit whose work dir is gone.
            signal(SIGINT, SIG_IGN);
            signal(SIGTERM, SIG_IGN);
            removeWorkDir(unit.workDir);
        }
    }
    catch (const exception& e) {
        say("ERROR:writing results: %s", e.what());
        code = FILE_IO_ERROR;
    }
    say("Folding@home Core Shutdown: %s", exitName(code));
    return code;
}
