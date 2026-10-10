# Prior art and market review (preliminary, not a freedom-to-operate or patentability search)

**Search date:** 2026-10-10. **Method:** web search through the Claude Code `WebSearch` tool (summaries of search-result pages; **the linked documents were not read in full**). **Scope:** public web only; no patent database queries beyond what the search engine returned; no non-English search; no academic database search. A search that finds nothing proves nothing; a search that finds something is not a legal conclusion.

## Queries and what came back

| # | Query (abridged) | Relevant results (title, URL) | Observation (technical, not legal) |
|---|---|---|---|
| 1 | self-similar recursive rhythm subdivision generator nested pattern sequencer | "Systems and methods of procedural media generation", USPTO publication 12469477 (https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/12469477); Cycling '74 Nestup concept and Blocks and Divs MIDI tools for Ableton (https://isotonikstudios.com/nested-tuplets-and-containerised-rhythms-in-live-with-midi-tools/); go-musicality rhythm trees (https://github.com/jamestunnell/go-musicality/releases); Kevin Jones, "Polkadotycat" (2003) recursive self-similar composition (https://eprints.kingston.ac.uk/997/); re<urse pattern language (https://fr.github.com/carrierdown/recurse) | Recursive subdivision of time into nested rhythm structures **is documented in several places** (tools, a library, a 2003 composition, at least one patent publication). Whether candidate C1 differs, and how, is **unresolved** and needs review of the full texts and claims. |
| 2 | audio slicer plugin generative beat variation locks mutate seed loop chop VST3 | ChopBeast 2 by W.A. Production (https://www.waproduction.com/plugins/view/chopbeast, https://rekkerd.org/w-a-production-releases-chopbeast-loop-slicer-plugin) | A commercial loop-chopping VST3 with randomization, per-slice locks and DAW export exists (launched 2025 per the page). Seed reproducibility was not stated in the summaries. Indicates a **real competing product category**. |
| 3 | Euclidean rhythms Toussaint paper; nested polyrhythm ratchet | Toussaint, "The Euclidean Algorithm Generates Traditional Musical Rhythms" (https://cgm.cs.mcgill.ca/~godfried/publications/banff.pdf); Demaine et al., rhythm CGTA (https://users.monash.edu/~davidwo/papers/Demaine-etal-Rhythm-CGTA.pdf) | Established algorithmic-rhythm literature; not the same method as C1, but part of the background art. No results on nested ratchets. |
| 4 | iZotope BreakTweaker / Ableton Beat Repeat / Slice to MIDI | iZotope BreakTweaker pages and reviews (https://www.izotope.com/en/learn/learn-breaktweaker-in-10-minutes, https://musictech.com/reviews/izotope-break-tweaker-review/) | BreakTweaker (now discontinued per iZotope) combined a step sequencer with a "MicroEdit" subdivision/slicing engine. Ableton results not returned. |

## Comparisons and gaps

- C1 (self-similar nested rhythm): closest found are nested-tuplet/rhythm-tree tools and the patent publication in query 1. **Not compared claim-by-claim.**
- C2-C6: no targeted search done yet. Deterministic cross-platform seeding and lock-aware regeneration are common engineering ideas; no distinct method is asserted.
- Market: loop-slicer plugins are an active category; sizing, pricing and demand were **not researched**.

## Unresolved questions

1. Full-text review of USPTO 12469477 and related patent families (classification codes for rhythm/pattern generation, G10H).
2. Database searches (Google Patents, Espacenet, Lens), non-English sources, DAFx/ICMC/NIME proceedings.
3. Whether any existing sequencer implements per-hit recursive application of the same pattern with zoom/lock semantics as in this project.
4. Dates of public disclosures of this project (see `README.md`) against any filing considerations.
