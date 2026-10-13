# Ownership and provenance (facts and open questions)

## Facts (from git history, 2026-10-10)

- 26 commits on `main`; all authored under the git identity "sheldon". First commit 2026-10-07 (specification documents), implementation merged 2026-10-08 and 2026-10-09 via squash-merged pull requests.
- At least 10 commits carry a `Co-Authored-By: Claude` trailer; the squash merges of PRs #1 to #3 contain the bulk of the implementation and were produced in AI-assisted (Claude Code) sessions directed by the repository owner. The specifications and prompts were directed by a human; which text and which design ideas originated with the human vs the AI **is not recorded**.
- `LICENSE` names `djshellshoxxx` as copyright holder ("Copyright (c) 2026 djshellshoxxx", All Rights Reserved). No contributor agreements, assignments or employer statements are in the repository.

## Open questions (do not guess)

1. Is the work owned by the individual, by Circuit Drift Labs, or by an employer or other party? Is there any assignment, employment or contractor agreement that applies?
2. How does AI-assisted authorship affect copyright and inventorship in the relevant jurisdictions? Which human contributions (selection, design direction, specifications, review) are documentable?
3. Were any code snippets, algorithms or text copied from external sources by the assistant? (No third-party files are vendored; snippet-level provenance is unverified.)
4. Who are all human contributors? (Only one git identity appears.)
5. Date of conception for each candidate in `technical-contributions.md`: unknown; the earliest repository evidence is 2026-10-07.
6. Does the owner want a copyright notice for Circuit Drift Labs? Not added: ownership evidence is insufficient, and existing license/notice text was left untouched.

## Records to start keeping (low effort)

Dated design notes outside the public repo; a contributor log stating who decided what; saved prompts/specs from AI-assisted sessions; any agreements establishing ownership.
