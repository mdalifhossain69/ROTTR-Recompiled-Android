// PE32+ image mapper for the guest memory manager. This maps and inspects a
// user-supplied executable; it never bypasses store or ownership checks.
#pragma once

#include "rottr/guest.h"
#include "rottr/guest_mem.h"
#include "rottr/x64_runtime.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rottr {

enum class PeLoadError : uint8_t {
    None,
    InvalidDosHeader,
    InvalidPeSignature,
    TruncatedHeaders,
    UnsupportedOptionalHeader,
    TooManySections,
    InvalidSectionTable,
    SectionOutOfRange,
    TruncatedSectionData,
    ImageOutOfRange,
    AllocationFailed,
    InvalidRelocationTable,
    UnsupportedRelocation,
    InvalidImportTable,
    InvalidExportTable,
    NotLoaded
};

const char* describePeLoadError(PeLoadError error);

struct PeSectionInfo {
    std::string name;
    uint32_t rva = 0;
    uint32_t virtualSize = 0;
    uint32_t rawSize = 0;
    uint32_t rawOffset = 0;
    uint32_t characteristics = 0;
    uint32_t protection = 0;  // ROTTR_MEM_* bits applied to this section
};

struct PeImportRequest {
    std::string module;
    std::string function;
    uint16_t ordinal = 0;
    bool byOrdinal = false;
    uint64_t iatRva = 0;
};

struct PeExport {
    std::string name;
    uint16_t ordinal = 0;
    uint64_t address = 0;       // guest address, or 0 for a forwarded export
    std::string forwarder;      // "DLL.Function" / "DLL.#ordinal" when forwarded
};

struct PeMissingImport {
    std::string description;    // "KERNEL32.dll!Sleep" or "KERNEL32.dll!#17"
    uint64_t trapAddress = 0;   // guest address of the trap thunk
    uint64_t iatRva = 0;
};

using PeImportResolver = std::function<bool(const PeImportRequest&, uint64_t& address)>;

struct PeLoadOptions {
    uint64_t loadBase = 0;               // guest address, 64 KB aligned; 0 picks the image's preferred base
    bool applyRelocations = true;        // Step 10: Apply DIR64 relocations
    bool resolveImports = true;          // Step 12: Resolve imports with traps
    bool applyProtections = true;        // Step 11: Apply section protections
    PeImportResolver importResolver;     // Import resolver callback

    // Step 15: PEB/TEB/GS base setup
    bool setupPEBTEB = false;           // Create Process/Thread Environment Blocks
    // Step 16: TLS callbacks
    bool setupTLS = false;               // Set up Thread Local Storage
    // Step 17: DllMain calling
    bool callDllMain = false;            // Call DllMain for loaded modules
    // Step 18: Import-free EXE loading
    bool entryReturns42 = false;         // Load EXE that returns 42 from entry point
};

class PeImage {
public:
    PeImage() = default;
    ~PeImage();
    PeImage(const PeImage&) = delete;
    PeImage& operator=(const PeImage&) = delete;
    PeImage(PeImage&& other) noexcept;
    PeImage& operator=(PeImage&& other) noexcept;

    // Maps `file` into `memory`. Every read of the untrusted file is bounds
    // checked; a truncated section table or payload is rejected, never padded
    // with data from outside the file.
    PeLoadError load(rottr_guest_mem* memory, const uint8_t* file, size_t fileSize,
                     const PeLoadOptions& options = {});
    void reset();

    bool loaded() const { return loaded_; }
    rottr_guest_mem* memory() const { return memory_; }
    uint64_t base() const { return base_; }
    uint64_t preferredBase() const { return preferredBase_; }
    uint32_t imageSize() const { return imageSize_; }
    uint32_t entryRva() const { return entryRva_; }
    uint64_t entryPoint() const { return loaded_ ? base_ + entryRva_ : 0; }
    bool isDll() const { return isDll_; }
    uint64_t trapBase() const { return trapBase_; }

    const std::vector<PeSectionInfo>& sections() const { return sections_; }
    const std::vector<PeImportRequest>& imports() const { return imports_; }
    const std::vector<PeMissingImport>& missingImports() const { return missingImports_; }
    const std::vector<PeExport>& exports() const { return exports_; }

    // Guest memory is authoritative: the host pointer is never used for guest
    // reads/writes unless the page check succeeds.
    bool read(uint64_t address, void* dst, size_t size) const;
    bool write(uint64_t address, const void* src, size_t size) const;

    bool vaToRva(uint64_t va, uint32_t& rva) const;
    bool findExport(const std::string& name, PeExport& result) const;
    bool findExportByOrdinal(uint16_t ordinal, PeExport& result) const;

    // Returns the "DLL!function" description when `address` is a missing-import
    // trap thunk. An empty string means the address is not a trap.
    std::string missingImportAt(uint64_t address) const;

    // TLS directory (data directory 9). VAs are image-relative virtual
    // addresses; all fields are zero when the image has no TLS directory.
    bool hasTls() const { return tlsDirectorySize_ != 0; }
    uint64_t tlsStart() const { return tlsStart_; }
    uint64_t tlsEnd() const { return tlsEnd_; }
    uint64_t tlsIndex() const { return tlsIndex_; }
    uint64_t tlsCallbacks() const { return tlsCallbacks_; }
    uint32_t tlsZeroFill() const { return tlsZeroFill_; }

    // Guest addresses of all TLS callbacks (empty when no TLS directory or
    // the callback array is empty). Reads guest memory directly.
    std::vector<uint64_t> tlsCallbackAddresses() const;

    // Runs every TLS callback for this image in order (step 16). Each
    // callback is called as (imageBase, DLL_PROCESS_ATTACH, 0). Returns
    // false if any callback faults. The CPU's RIP/RSP are preserved across
    // each call.
    bool runTlsCallbacks(rottr_cpu& cpu, const X64GuestMemory& guest) const;

    // Calls DllMain at this image's entry point with the given reason
    // (step 17). Returns false if the call faults; outResult receives the
    // BOOL return value. CPU RIP/RSP are preserved.
    bool callDllMain(rottr_cpu& cpu, const X64GuestMemory& guest,
                     uint32_t reason, uint64_t& outResult) const;

    std::string describe() const;

private:
    friend class PeModuleRegistry;

    rottr_guest_mem* memory_ = nullptr;
    uint64_t base_ = 0;
    uint64_t preferredBase_ = 0;
    uint64_t trapBase_ = 0;
    size_t trapBytes_ = 0;
    uint32_t imageSize_ = 0;
    uint32_t headersSize_ = 0;
    uint32_t entryRva_ = 0;
    bool isDll_ = false;
    bool loaded_ = false;

    uint32_t exportDirRva_ = 0;
    uint32_t exportDirSize_ = 0;
    uint64_t tlsStart_ = 0;
    uint64_t tlsEnd_ = 0;
    uint64_t tlsIndex_ = 0;
    uint64_t tlsCallbacks_ = 0;
    uint32_t tlsDirectorySize_ = 0;
    uint32_t tlsZeroFill_ = 0;

    std::vector<PeSectionInfo> sections_;
    std::vector<PeImportRequest> imports_;
    std::vector<PeMissingImport> missingImports_;
    std::vector<PeExport> exports_;
};

// Loads DLLs from the user's own game folder. The opener is supplied by the
// application; nothing is read from the network or from bundled game data.
class PeModuleRegistry {
public:
    explicit PeModuleRegistry(rottr_guest_mem* memory) : memory_(memory) {}

    using FileOpener = std::function<bool(const std::string& name, std::vector<uint8_t>& bytes)>;
    void setFileOpener(FileOpener opener) { opener_ = std::move(opener); }

    // Takes ownership of an already mapped image (used for the main EXE).
    PeImage* addImage(std::unique_ptr<PeImage> image, const std::string& name);
    PeImage* findModule(const std::string& name) const;
    PeImage* loadModule(const std::string& name, std::string* error,
                        const PeLoadOptions& options = {});
    // Used as the import resolver of the main EXE and of every guest DLL.
    bool resolveImport(const PeImportRequest& request, uint64_t& address);

    // Order in which module entry points must run: dependencies first.
    std::vector<PeImage*> initializationOrder() const;
    int refCount(const std::string& name) const;

    // Step 16: Invoke a loaded module's TLS callbacks (DLL_PROCESS_ATTACH).
    // Returns false if any callback faults.
    bool runTlsCallbacks(PeImage* image, rottr_cpu& cpu,
                         const X64GuestMemory& memory);

    // Step 17: Call each loaded DLL's DllMain with DLL_PROCESS_ATTACH in
    // dependency order. Returns false if an entry point faults or rejects load.
    bool callEntryPoints(rottr_cpu& cpu, const X64GuestMemory& memory);
    size_t moduleCount() const { return modules_.size(); }
    std::string describe() const;

private:
    struct Module {
        std::string name;
        std::unique_ptr<PeImage> image;
        int refs = 0;
        bool loading = false;
        bool failed = false;
        std::vector<std::string> dependencies;
    };

    static std::string normalize(const std::string& name);
    Module* findModuleEntry(const std::string& name);
    const Module* findModuleEntry(const std::string& name) const;
    bool resolveExport(PeImage* image, const std::string& name, uint16_t ordinal,
                       bool byOrdinal, uint64_t& address, int depth);

    rottr_guest_mem* memory_;
    FileOpener opener_;
    std::vector<Module> modules_;
};

}  // namespace rottr
