export const meta = {
  name: 'milestone-audit',
  description: 'Adversarial architecture audit of a milestone increment against the accepted ADRs',
  phases: [
    { title: 'Survey', detail: 'four lenses over the increment' },
    { title: 'Verify', detail: 'adversarial refutation of the top findings' },
    { title: 'Synthesize', detail: 'audit summary, story and gate proposals' },
  ],
}

const { milestone, baseRef, headRef } = args || {}
if (!milestone || !baseRef) {
  throw new Error('args required: { milestone: "M5", baseRef: "v0.2.0", headRef?: "HEAD" }')
}
const range = `${baseRef}..${headRef || 'HEAD'}`

const FINDINGS = {
  type: 'object',
  required: ['findings'],
  properties: {
    findings: {
      type: 'array',
      maxItems: 4,
      items: {
        type: 'object',
        required: ['title', 'file', 'evidence', 'why_it_matters', 'proposed_action', 'severity'],
        properties: {
          title: { type: 'string' },
          file: { type: 'string' },
          line: { type: 'integer' },
          evidence: { type: 'string' },
          why_it_matters: { type: 'string' },
          proposed_action: { type: 'string' },
          severity: { enum: ['high', 'medium', 'low'] },
        },
      },
    },
  },
}

const VERDICT = {
  type: 'object',
  required: ['refuted', 'reasoning'],
  properties: {
    refuted: { type: 'boolean' },
    reasoning: { type: 'string' },
  },
}

const SYNTHESIS = {
  type: 'object',
  required: ['audit_summary_markdown', 'story_proposals', 'gate_proposals'],
  properties: {
    audit_summary_markdown: { type: 'string' },
    story_proposals: {
      type: 'array',
      items: {
        type: 'object',
        required: ['title', 'size', 'rationale'],
        properties: {
          title: { type: 'string' },
          size: { enum: ['S', 'M', 'L'] },
          rationale: { type: 'string' },
        },
      },
    },
    gate_proposals: { type: 'array', items: { type: 'string' } },
  },
}

const ctx =
  `You are auditing the ${milestone} increment of the Revenant repository ` +
  `(commit range ${range}). First read AGENTS.md and the "Milestone-level architecture ` +
  `audit" section of docs/code-quality.md. Use git log/diff over the range, then read ` +
  `the current files it touches. Read-only: never edit anything. Report at most 4 ` +
  `findings — only ones that would change what we build next — and cite file:line ` +
  `evidence for each.`

const LENSES = [
  {
    key: 'layer-leakage',
    prompt:
      'Has any layer (core, volume, fs, carve, recovery, cli) leaked responsibility ' +
      'into another during this increment? Inspect new includes and dependencies ' +
      'between src/ subtrees in the range, and interfaces that widened to accommodate ' +
      'a neighbor.',
  },
  {
    key: 'adr-conformance',
    prompt:
      'Does the increment still satisfy every accepted ADR in docs/architecture/adr/? ' +
      'Weigh especially ADR-0003 (validating carving, precision over recall), ADR-0005 ' +
      '(read-only source), and ADR-0007 (block-level access boundary). A finding is a ' +
      'place where reality diverged from an ADR, or where the increment demanded a new ' +
      'seam no ADR records (that finding proposes a new ADR).',
  },
  {
    key: 'complexity-creep',
    prompt:
      'Where has complexity crept in that a refactor should remove before scope ' +
      'widens? Files near the 250-line limit, functions crowding the 10-statement cap, ' +
      'duplicated knowledge, God-object tendencies, primitive obsession. Prioritize ' +
      'what will hurt the NEXT milestone.',
  },
  {
    key: 'recurring-findings',
    prompt:
      `Sweep the increment history (git log ${range}, CHANGELOG.md) for repeated ` +
      'classes of fix — the same mistake fixed more than once. Each recurring class is ' +
      'a candidate for a new automated check; say what the check would be and where it ' +
      'would run (clang-tidy config, a lint script, CI).',
  },
]

phase('Survey')
const surveyed = await parallel(
  LENSES.map((l) => () =>
    agent(`${ctx}\n\nLens: ${l.key}. ${l.prompt}`, {
      label: `survey:${l.key}`,
      phase: 'Survey',
      schema: FINDINGS,
    }).then((r) => (r ? r.findings.map((f) => ({ ...f, lens: l.key })) : []))
  )
)
const all = surveyed.filter(Boolean).flat()
log(`${all.length} raw findings across ${LENSES.length} lenses`)

// Barrier justified: dedup needs every lens's findings before verification.
const seen = new Set()
const unique = all.filter((f) => {
  const k = `${f.file}|${f.title.toLowerCase().slice(0, 40)}`
  if (seen.has(k)) return false
  seen.add(k)
  return true
})
const rank = { high: 0, medium: 1, low: 2 }
unique.sort((a, b) => rank[a.severity] - rank[b.severity])
const toVerify = unique.slice(0, 8)
const unverified = unique.slice(8)
if (unverified.length > 0) {
  log(`verification capped at 8; ${unverified.length} lower-severity finding(s) pass through unverified`)
}

phase('Verify')
const verified = await parallel(
  toVerify.map((f) => () =>
    agent(
      `${ctx}\n\nAdversarially try to REFUTE this audit finding. Reread the cited ` +
        `evidence and its surrounding code; check whether it is real, already handled, ` +
        `or misread. Default to refuted=true if uncertain.\n\n${JSON.stringify(f)}`,
      { label: `refute:${f.lens}`, phase: 'Verify', schema: VERDICT }
    ).then((v) => (v ? { ...f, verdict: v } : null))
  )
)
const judged = verified.filter(Boolean)
const confirmed = judged.filter((f) => !f.verdict.refuted)
const refuted = judged.filter((f) => f.verdict.refuted)
log(`${confirmed.length} confirmed, ${refuted.length} refuted`)

phase('Synthesize')
const material = {
  milestone,
  range,
  confirmed,
  unverified,
  refuted: refuted.map((f) => ({ title: f.title, reasoning: f.verdict.reasoning })),
}
const synthesis = await agent(
  `${ctx}\n\nYou are the synthesizer. From the findings JSON below, produce: ` +
    `(1) audit_summary_markdown answering the four questions of the milestone-level ` +
    `audit in docs/code-quality.md; (2) one story proposal per confirmed finding — ` +
    `title in the backlog's style plus size S/M/L and a one-paragraph rationale ` +
    `(numbers are allocated later, per docs/backlog/README.md, so titles only); ` +
    `(3) gate_proposals for any recurring class that deserves an automated check.\n\n` +
    JSON.stringify(material),
  { label: 'synthesize', phase: 'Synthesize', schema: SYNTHESIS }
)

return { milestone, range, confirmed, unverified, refuted: material.refuted, synthesis }
