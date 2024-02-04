//------------------------------------------------------------------------------
//! @file SourceManager.h
//! @brief Source file management
//
// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <expected.hpp>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <span>
#include <variant>
#include <vector>

#include "slang/text/SourceLocation.h"
#include "slang/util/Hash.h"
#include "slang/util/SmallVector.h"
#include "slang/util/Util.h"

namespace slang {

enum class DiagnosticSeverity;

template<typename T>
concept IsLock = std::is_same_v<T, std::shared_lock<std::shared_mutex>> ||
                 std::is_same_v<T, std::unique_lock<std::shared_mutex>>;

/// SourceManager - Handles loading and tracking source files.
///
/// The source manager abstracts away the differences between
/// locations in files and locations generated by macro expansion.
/// See SourceLocation for more details.
///
/// The methods in this class are thread safe unless otherwise noted.
class SLANG_EXPORT SourceManager {
public:
    using BufferOrError = nonstd::expected<SourceBuffer, std::error_code>;

    /// Default constructor.
    SourceManager();
    SourceManager(const SourceManager&) = delete;
    SourceManager& operator=(const SourceManager&) = delete;

    /// @brief Adds one or more system include directories that match
    /// the given pattern.
    ///
    /// @returns An error code if the given pattern is for an exact path
    /// and that path does not exist or is not a directory.
    std::error_code addSystemDirectories(std::string_view pattern);

    /// @brief Adds one or more user include directories that match
    /// the given pattern.
    ///
    /// @returns An error code if the given pattern is for an exact path
    /// and that path does not exist or is not a directory.
    std::error_code addUserDirectories(std::string_view pattern);

    /// Gets the source line number for a given source location.
    size_t getLineNumber(SourceLocation location) const;

    /// Gets the source file name for a given source location.
    std::string_view getFileName(SourceLocation location) const;

    /// Gets the source file name for a given source buffer, not taking
    /// into account any `line directives that may be in the file.
    std::string_view getRawFileName(BufferID buffer) const;

    /// Gets the full path to the given source buffer. This does not take
    /// into account any `line directives. If the buffer is not a file buffer,
    /// returns an empty string.
    const std::filesystem::path& getFullPath(BufferID buffer) const;

    /// Gets the column line number for a given source location.
    /// @a location must be a file location.
    size_t getColumnNumber(SourceLocation location) const;

    /// Gets a location that indicates from where the given buffer was included.
    /// @a location must be a file location.
    SourceLocation getIncludedFrom(BufferID buffer) const;

    /// Gets the source library of which the given buffer is a part,
    /// or nullptr if it's not explicitly part of any library.
    const SourceLibrary* getLibraryFor(BufferID buffer) const;

    /// Attempts to get the name of the macro represented by a macro location.
    /// If no macro name can be found, returns an empty string view.
    std::string_view getMacroName(SourceLocation location) const;

    /// Determines whether the given location exists in a source file.
    bool isFileLoc(SourceLocation location) const;

    /// Determines whether the given location points to a macro expansion.
    bool isMacroLoc(SourceLocation location) const;

    /// Determines whether the given location points to a macro argument expansion.
    bool isMacroArgLoc(SourceLocation location) const;

    /// Determines whether the given location is inside an include file.
    bool isIncludedFileLoc(SourceLocation location) const;

    /// Determines whether the given location is from a macro expansion or an include file.
    bool isPreprocessedLoc(SourceLocation location) const;

    /// Gets the expansion location of a given macro location.
    SourceLocation getExpansionLoc(SourceLocation location) const;

    /// Gets the expansion range of a given macro location.
    SourceRange getExpansionRange(SourceLocation location) const;

    /// Gets the original source location of a given macro location.
    SourceLocation getOriginalLoc(SourceLocation location) const;

    /// Gets the actual original location where source is written, given a location
    /// inside a macro. Otherwise just returns the location itself.
    SourceLocation getFullyOriginalLoc(SourceLocation location) const;

    /// If the given location is a macro location, fully expands it out to its actual
    /// file expansion location. Otherwise just returns the location itself.
    SourceLocation getFullyExpandedLoc(SourceLocation location) const;

    /// Gets the actual source text for a given file buffer.
    std::string_view getSourceText(BufferID buffer) const;

    /// Creates a macro expansion location; used by the preprocessor.
    SourceLocation createExpansionLoc(SourceLocation originalLoc, SourceRange expansionRange,
                                      bool isMacroArg);

    /// Creates a macro expansion location; used by the preprocessor.
    SourceLocation createExpansionLoc(SourceLocation originalLoc, SourceRange expansionRange,
                                      std::string_view macroName);

    /// Instead of loading source from a file, copy it from text already in memory.
    SourceBuffer assignText(std::string_view text, SourceLocation includedFrom = SourceLocation(),
                            const SourceLibrary* library = nullptr);

    /// Instead of loading source from a file, copy it from text already in memory.
    /// Pretend it came from a file located at @a path.
    SourceBuffer assignText(std::string_view path, std::string_view text,
                            SourceLocation includedFrom = SourceLocation(),
                            const SourceLibrary* library = nullptr);

    /// Instead of loading source from a file, move it from text already in memory.
    /// Pretend it came from a file located at @a path.
    SourceBuffer assignBuffer(std::string_view path, SmallVector<char>&& buffer,
                              SourceLocation includedFrom = SourceLocation(),
                              const SourceLibrary* library = nullptr);

    /// Read in a source file from disk.
    BufferOrError readSource(const std::filesystem::path& path, const SourceLibrary* library);

    /// Read in a header file from disk.
    BufferOrError readHeader(std::string_view path, SourceLocation includedFrom,
                             const SourceLibrary* library, bool isSystemPath,
                             std::span<std::filesystem::path const> additionalIncludePaths);

    /// Returns true if the given file path is already loaded and cached in the source manager.
    bool isCached(const std::filesystem::path& path) const;

    /// Sets whether filenames should be made "proximate" to the current directory
    /// for diagnostic reporting purposes. This is on by default but can be
    /// disabled to always use the simple filename.
    void setDisableProximatePaths(bool set) { disableProximatePaths = set; }

    /// Adds a line directive at the given location.
    void addLineDirective(SourceLocation location, size_t lineNum, std::string_view name,
                          uint8_t level);

    /// Adds a diagnostic directive at the given location.
    void addDiagnosticDirective(SourceLocation location, std::string_view name,
                                DiagnosticSeverity severity);

    /// Stores information specified in a `pragma diagnostic directive, which alters the
    /// currently active set of diagnostic mappings.
    struct DiagnosticDirectiveInfo {
        /// The name of the diagnostic being controlled.
        std::string_view name;

        /// Offset in the source where the directive occurred.
        size_t offset;

        /// The new severity the diagnostic should have.
        DiagnosticSeverity severity;

        DiagnosticDirectiveInfo(std::string_view name, size_t offset,
                                DiagnosticSeverity severity) noexcept :
            name(name),
            offset(offset), severity(severity) {}
    };

    /// Visits each buffer that contains diagnostic directives and invokes the provided
    /// callback with the first argument being the buffer and the second being an
    /// iterable collection of DiagnosticDirectiveInfos.
    template<typename Func>
    void visitDiagnosticDirectives(Func&& func) const {
        std::shared_lock lock(mutex);
        for (auto& [buffer, directives] : diagDirectives)
            func(buffer, directives);
    }

    /// Gets the diagnostic directives associated with the given buffer, if any.
    /// @warning This method is not thread safe. The returned span is also not safe
    /// to store; the underlying data can be mutated by a call to
    /// @a addDiagnosticDirective and invalidate the span.
    std::span<const DiagnosticDirectiveInfo> getDiagnosticDirectives(BufferID buffer) const;

    /// Returns a list of buffers (files and macros) that have been created in the
    /// source manager.
    std::vector<BufferID> getAllBuffers() const;

private:
    // Stores information specified in a `line directive, which alters the
    // line number and file name that we report in diagnostics.
    struct LineDirectiveInfo {
        std::string name;       // File name set by directive
        size_t lineInFile;      // Actual file line where directive occurred
        size_t lineOfDirective; // Line number set by directive
        uint8_t level;          // Level of directive. Either 0, 1, or 2.

        LineDirectiveInfo(std::string&& fname, size_t lif, size_t lod, uint8_t level) noexcept :
            name(std::move(fname)), lineInFile(lif), lineOfDirective(lod), level(level) {}
    };

    // Stores actual file contents and metadata; only one per loaded file
    struct FileData {
        const std::string name;                       // name of the file
        const SmallVector<char> mem;                  // file contents
        std::vector<size_t> lineOffsets;              // cache of compute line offsets
        const std::filesystem::path* const directory; // directory in which the file exists
        const std::filesystem::path fullPath;         // full path to the file

        FileData(const std::filesystem::path* directory, std::string name, SmallVector<char>&& data,
                 std::filesystem::path fullPath) :
            name(std::move(name)),
            mem(std::move(data)), directory(directory), fullPath(std::move(fullPath)) {}
    };

    // Stores a pointer to file data along with information about where we included it.
    // There can potentially be many of these for a given file.
    struct FileInfo {
        FileData* data = nullptr;
        const SourceLibrary* library = nullptr;
        SourceLocation includedFrom;
        std::vector<LineDirectiveInfo> lineDirectives;

        FileInfo() {}
        FileInfo(FileData* data, const SourceLibrary* library, SourceLocation includedFrom) :
            data(data), library(library), includedFrom(includedFrom) {}

        // Returns a pointer to the LineDirectiveInfo for the nearest enclosing
        // line directive of the given raw line number, or nullptr if there is none
        const LineDirectiveInfo* getPreviousLineDirective(size_t rawLineNumber) const;
    };

    // Instead of a file, this lets a BufferID point to a macro expansion location.
    // This is actually used two different ways; if this is a normal token from a
    // macro expansion, originalLocation will point to the token inside the macro
    // definition, and expansionLocation will point to the range of the macro usage
    // at the expansion site. Alternatively, if this token came from an argument,
    // originalLocation will point to the argument at the expansion site and
    // expansionLocation will point to the parameter inside the macro body.
    struct ExpansionInfo {
        SourceLocation originalLoc;
        SourceRange expansionRange;
        bool isMacroArg = false;

        std::string_view macroName;

        ExpansionInfo() {}
        ExpansionInfo(SourceLocation originalLoc, SourceRange expansionRange, bool isMacroArg) :
            originalLoc(originalLoc), expansionRange(expansionRange), isMacroArg(isMacroArg) {}

        ExpansionInfo(SourceLocation originalLoc, SourceRange expansionRange,
                      std::string_view macroName) :
            originalLoc(originalLoc),
            expansionRange(expansionRange), macroName(macroName) {}
    };

    // This mutex protects pretty much everything in this class.
    mutable std::shared_mutex mutex;

    // This mutex is specifically for protecting the system and user
    // include directory lists.
    mutable std::shared_mutex includeDirMutex;

    // index from BufferID to buffer metadata
    std::vector<std::variant<FileInfo, ExpansionInfo>> bufferEntries;

    // cache for file lookups; this holds on to the actual file data
    flat_hash_map<std::string, std::pair<std::unique_ptr<FileData>, std::error_code>> lookupCache;

    // directories for system and user includes
    std::vector<std::filesystem::path> systemDirectories;
    std::vector<std::filesystem::path> userDirectories;

    // uniquified backing memory for directories
    std::set<std::filesystem::path> directories;

    // map from buffer to diagnostic directive lists
    flat_hash_map<BufferID, std::vector<DiagnosticDirectiveInfo>> diagDirectives;

    std::atomic<uint32_t> unnamedBufferCount = 0;
    bool disableProximatePaths = false;

    template<IsLock TLock>
    FileInfo* getFileInfo(BufferID buffer, TLock& lock);

    template<IsLock TLock>
    const FileInfo* getFileInfo(BufferID buffer, TLock& lock) const;

    SourceBuffer createBufferEntry(FileData* fd, SourceLocation includedFrom,
                                   const SourceLibrary* library,
                                   std::unique_lock<std::shared_mutex>& lock);

    BufferOrError openCached(const std::filesystem::path& fullPath, SourceLocation includedFrom,
                             const SourceLibrary* library);
    SourceBuffer cacheBuffer(std::filesystem::path&& path, std::string&& pathStr,
                             SourceLocation includedFrom, const SourceLibrary* library,
                             SmallVector<char>&& buffer);

    template<IsLock TLock>
    size_t getRawLineNumber(SourceLocation location, TLock& lock) const;

    template<IsLock TLock>
    bool isMacroLocImpl(SourceLocation location, TLock& lock) const;

    template<IsLock TLock>
    bool isMacroArgLocImpl(SourceLocation location, TLock& lock) const;

    template<IsLock TLock>
    SourceLocation getFullyExpandedLocImpl(SourceLocation location, TLock& lock) const;

    template<IsLock TLock>
    SourceLocation getOriginalLocImpl(SourceLocation location, TLock& lock) const;

    template<IsLock TLock>
    SourceRange getExpansionRangeImpl(SourceLocation location, TLock& lock) const;

    static void computeLineOffsets(const SmallVector<char>& buffer,
                                   std::vector<size_t>& offsets) noexcept;
};

} // namespace slang
