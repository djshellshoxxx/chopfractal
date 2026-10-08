# Variation Family Tree

**Status:** Creation spec for review  
**Module ID:** `variation_history`  
**Purpose:** Preserve generated and edited variations as a navigable family tree, so users can branch from any promising groove and return to earlier versions.

## User behavior

Every Generate or Mutate action creates a child variation from the active node. The tree records parentage, seed, label, and a compact summary. Selecting a node loads its exact pattern state for audition. The user can branch from an earlier node, compare two nodes, rename, favorite, and delete a branch.

The feature is a history browser, not merely a larger undo stack. Undo remains for immediate editing. The family tree preserves named creative paths across multiple mutations.

## Snapshot model

Keep the module generic: it stores immutable payload bytes supplied by the owning pattern/state serializer, plus metadata:
- Stable node ID and parent node ID.
- Payload hash and module schema version.
- Seed and optional user label.
- Creation ordering value; wall-clock timestamp is display-only.
- Optional tags/favorite flag.

Use complete snapshots for the initial implementation; pattern snapshots are small, and this avoids fragile delta reconstruction. Enforce a default cap of 64 nodes per project, configurable to 128. When full, prune oldest unpinned leaf nodes first. Never prune the active node, its required ancestor chain, favorites, or a node without telling the user. If no safe prune candidate exists, refuse the new snapshot with a clear limit message.

Manual edits are coalesced: Generate, Mutate, and explicit Save Snapshot create nodes immediately. Continuous dragging does not create a node for every mouse movement; create one after the edit settles or when the user commits it.

## Module API and integration

The module has no audio, UI, host, or ChopFractal data dependency. A generic API accepts payload bytes and metadata:
- `addSnapshot(parentId, payload, metadata) -> NodeId`
- `getSnapshot(nodeId) -> Snapshot`
- `listChildren(parentId) -> NodeSummaryList`
- `compare(nodeA, nodeB) -> SummaryDiff` when a caller-supplied diff strategy is available
- `deleteBranch(nodeId, prunePolicy) -> Result`

The pattern serializer creates/loads the payload. The composition root handles save/load, A/B audition, and quantized activation. The UI renders nodes as a tree or branch map. For playback-safe switching, apply a selected pattern at the next loop boundary by default, with an explicit immediate switch option that uses a short crossfade if supported.

## Tests and acceptance

- Each node restores the exact payload, seed, and state schema.
- Branching from a parent leaves sibling nodes unchanged.
- Duplicate payload hashes may be deduplicated only when parentage and metadata semantics remain clear.
- Pruning never removes protected nodes or required ancestors.
- Corrupt payloads fail safely and identify the affected node.
- Serialization round-trips the tree and its parent links.
- Selecting a node during playback changes at a safe boundary or follows the documented immediate-switch behavior.

## Migration to another project

Copy the generic `modules/variation_history/` package; it should compile without `chop_contracts`, JUCE, or audio code. The receiving project supplies its own serializer and provides a byte payload plus metadata. Add the module's CMake target, choose a node cap, wire its own snapshot selection UI, and test round-trip, branch, prune, and restore behavior. This module can support alternate presets, scene states, sound-design branches, and generative sequences. Preserve the module ID and schema version; follow `MIGRATION.md` for an example.
