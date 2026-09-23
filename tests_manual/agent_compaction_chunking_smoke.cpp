// Manual smoke test for Agent::computeCompactionChunks - the piece that
// keeps a single compaction summarization call bounded in size no matter
// how large a session's aged-out backlog is (issue: a session's very
// first compaction, most commonly one that already had a large history
// before compaction existed, could otherwise need a single LLM call sized
// to the whole backlog and overflow the model's context window).
#include "atlas/agent/Agent.hpp"

#include <iostream>
#include <string>

using atlas::agent::Agent;

namespace {
int failures = 0;

void expect(bool cond, const std::string& label) {
    std::cout << (cond ? "ok: " : "FAIL: ") << label << "\n";
    if (!cond) ++failures;
}
} // namespace

int main() {
    // -----------------------------------------------------------------
    // Nothing to summarize: no chunks at all.
    // -----------------------------------------------------------------
    {
        auto chunks = Agent::computeCompactionChunks(0, 40);
        expect(chunks.empty(), "zero backlog: no chunks");
    }

    // -----------------------------------------------------------------
    // Backlog smaller than a single chunk: exactly one chunk covering it
    // all, not padded or split needlessly.
    // -----------------------------------------------------------------
    {
        auto chunks = Agent::computeCompactionChunks(15, 40);
        expect(chunks.size() == 1, "small backlog: exactly one chunk");
        expect(chunks[0].first == 0 && chunks[0].second == 15,
               "small backlog: single chunk covers the whole backlog");
    }

    // -----------------------------------------------------------------
    // Backlog exactly a multiple of the chunk size: divides evenly, no
    // trailing empty/short chunk.
    // -----------------------------------------------------------------
    {
        auto chunks = Agent::computeCompactionChunks(120, 40);
        expect(chunks.size() == 3, "exact multiple: divides evenly into 3 chunks");
        expect(chunks[0] == std::make_pair(std::size_t{0}, std::size_t{40}), "exact multiple: chunk 1 is [0,40)");
        expect(chunks[1] == std::make_pair(std::size_t{40}, std::size_t{80}), "exact multiple: chunk 2 is [40,80)");
        expect(chunks[2] == std::make_pair(std::size_t{80}, std::size_t{120}), "exact multiple: chunk 3 is [80,120)");
    }

    // -----------------------------------------------------------------
    // The actual bug this exists to fix: a large pre-existing backlog
    // (e.g. a session that had 460 messages before compaction ever ran)
    // gets bounded into several chunks of at most the chunk size each,
    // rather than one chunk sized to the whole 460-message backlog.
    // -----------------------------------------------------------------
    {
        auto chunks = Agent::computeCompactionChunks(460, 40);
        expect(chunks.size() == 12, "large first-time backlog: splits into bounded chunks (12, not 1)");
        for (const auto& [start, end] : chunks) {
            expect(end - start <= 40, "large first-time backlog: every chunk is within the bound");
        }
        expect(chunks.front().first == 0, "large first-time backlog: first chunk starts at 0");
        expect(chunks.back().second == 460, "large first-time backlog: last chunk ends at the full backlog size");
    }

    // -----------------------------------------------------------------
    // Chunks are contiguous and non-overlapping: each one picks up
    // exactly where the previous one left off.
    // -----------------------------------------------------------------
    {
        auto chunks = Agent::computeCompactionChunks(97, 40);
        for (std::size_t i = 1; i < chunks.size(); ++i) {
            expect(chunks[i].first == chunks[i - 1].second,
                   "contiguity: chunk " + std::to_string(i) + " starts exactly where the previous one ended");
        }
        expect(chunks.back().second == 97, "contiguity: chunks fully cover the backlog with no leftover");
    }

    // -----------------------------------------------------------------
    // A chunk size of 0 doesn't loop forever - treated as 1 instead.
    // -----------------------------------------------------------------
    {
        auto chunks = Agent::computeCompactionChunks(3, 0);
        expect(chunks.size() == 3, "zero chunk size: guarded to behave like a chunk size of 1, not an infinite loop");
    }

    std::cout << (failures == 0 ? "\nAll checks passed.\n" : "\nSome checks FAILED.\n");
    return failures == 0 ? 0 : 1;
}
