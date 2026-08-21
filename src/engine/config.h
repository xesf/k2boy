// config.h — startup configuration, as the original reads it.
//
// WinMain (0x42BD61) does one of two things before anything else:
//
//     lpCmdLine non-empty -> CommandLineParser__DispatchFlag(lpCmdLine)
//     otherwise           -> open "distribution.dat" and dispatch from that
//
// So the shipped build configures itself from a file of command-line flags. See
// docs/reference/boot-sequence.md.
//
// The original's dispatcher (0x45D0A7) scans to a '-', takes the **single next
// character**, lowercases it, and switches on that; each handler then parses
// whatever follows for itself. Multi-letter spellings like "-warn" are that
// one-character flag plus handler-specific tail, not distinct flags — see
// docs/reference/command-line.md.
//
// This mirrors that shape and stops there. Only about six of the eighteen
// handlers have been decoded, so the tail is captured verbatim rather than
// interpreted: storing the raw text is honest, inventing semantics is not.
//
// C++11. No gfx or platform dependency; the caller supplies the bytes.

#ifndef K2_ENGINE_CONFIG_H
#define K2_ENGINE_CONFIG_H

#include <stdint.h>
#include <string>
#include <vector>

namespace k2 {
namespace engine {

struct config_flag {
    char        key;     // the single dispatch character, lowercased
    std::string tail;    // everything the handler would parse, trimmed
};

struct config {
    std::vector<config_flag> flags;

    // First flag with this key, or null.
    const config_flag* find(char key) const;

    // Convenience for the flags whose meaning is established.
    std::string startup_file() const;   // -i  : "menu.dat" in the shipped file
    std::string console_file() const;   // -c  : "console.dat"
};

// Parses flag text in the original's style. Handles both a real command line and
// a flag *file*: ';' starts a comment to end of line, and line breaks are
// treated as whitespace, which is what distribution.dat relies on.
void config_parse(const char* text, size_t size, config& out);

} // namespace engine
} // namespace k2

#endif // K2_ENGINE_CONFIG_H
