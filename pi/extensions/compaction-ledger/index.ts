/**
 * Evidence-ledger compaction for the GoldenEye 007 PC port (pi/ package).
 * Targets the installed pi 1.0.2 extension API (SessionBeforeCompactResult);
 * the first-party examples/ in the npm package target a newer API — do not
 * copy from them without re-checking dist/core/extensions/types.d.ts.
 *
 * What it does:
 *
 *  1. Overrides the compaction summary with an evidence-ledger prompt: every
 *     live hypothesis about the bug being worked gets one line tagged
 *     refuted / unvalidated / validated(<artifact>). "Diagnosed" is
 *     forbidden without a maintainer A/B or a passing golden sweep.
 *     Motivated by pi session 01a11ec6 (the #150 cull hunt): the 04:52Z
 *     compaction promoted an unvalidated hypothesis to "diagnosed" and the
 *     post-compaction turns built the next three fix candidates on that
 *     false premise.
 *
 *  2. When GE_COMPACT_MODEL is set (fully-qualified "provider/model", e.g.
 *     "strata/strata-coder-hard" or "openrouter/deepseek/deepseek-v4-pro"),
 *     the summarization runs on that stronger model instead of the session
 *     model. Unset -> the session model (labor on a fast local model,
 *     compaction on a strong one).
 *
 * Failure mode is safe by design: if the custom summarization throws (model
 * down, no API key, shape drift after a pi upgrade), the handler returns
 * nothing and pi's built-in compaction proceeds unchanged.
 *
 * GE_COMPACT_LEDGER=0 disables the extension's override entirely.
 */
import { completeSimple } from "@earendil-works/pi-ai/compat";
import type { Model } from "@earendil-works/pi-ai";
import type {
  ExtensionAPI,
  ExtensionContext,
  SessionBeforeCompactEvent,
  SessionBeforeCompactResult,
} from "@earendil-works/pi-coding-agent";

const EVIDENCE_LEDGER_PROMPT = [
  "The summary MUST start with a section titled `Evidence ledger`. For every",
  "live hypothesis or open claim about the bug/work item being pursued, write",
  "exactly one line in this form:",
  "  - <claim> -- refuted | unvalidated | validated (<artifact>)",
  "where <artifact> is a concrete check: a named log file, a golden-sweep",
  "frame, a maintainer A/B, or a commit. Rules:",
  '  - A fix that has not passed a maintainer A/B or a golden sweep is',
  '"unvalidated", never "diagnosed" or "root-caused".',
  "  - Carry forward the previous summary's ledger lines that are still live;",
  "    do not drop a refutation just because it was summarized before.",
  "After the ledger, include: the uncommitted-tree state (files, which work",
  "item each belongs to), the next concrete step, and -- if the session's",
  "brief had a BUDGET line -- the remaining budget.",
].join("\n");

const SUMMARY_SYSTEM = [
  "You are writing the context-compaction summary for a coding-agent session.",
  "Produce a compact, dense handoff summary that a fresh session can resume",
  "work from. Preserve: the active task, refuted hypotheses (so they are not",
  "retried), the uncommitted tree state, standing environment rules, and the",
  "next concrete step. No filler, no restating this instruction.",
].join("\n");

// ---------------------------------------------------------------- transcript

type Msg = { role?: string; content?: unknown; toolName?: string; [k: string]: unknown };

function contentText(content: unknown): string {
  if (typeof content === "string") return content;
  if (Array.isArray(content)) {
    return content
      .map((p) => {
        if (p && typeof p === "object") {
          const o = p as Record<string, unknown>;
          if (o.type === "text") return (o.text as string) ?? "";
          if (o.type === "toolCall" || o.type === "tool_use")
            return `[tool call: ${(o.name as string) ?? "?"}]`;
          if (o.type === "image" || o.type === "image_url") return "[image]";
        }
        return "";
      })
      .join(" ");
  }
  return "";
}

function truncate(s: string, n: number): string {
  return s.length <= n ? s : s.slice(0, n) + ` [${s.length - n} chars elided]`;
}

function digestMessages(messages: Msg[], capChars: number): string {
  const lines: string[] = [];
  let budget = capChars;
  for (const m of messages) {
    const role = m?.role ?? "unknown";
    let text = contentText(m?.content).replace(/\s+/g, " ").trim();
    if (role === "toolResult" || m?.toolName) text = truncate(text, 400);
    const line = `[${role}] ${text}`.trim();
    if (budget - line.length - 1 < 0 && lines.length > 0) {
      lines.unshift(`[... ${messages.length - lines.length} earlier messages elided ...]`);
      break;
    }
    budget -= line.length + 1;
    lines.push(line);
  }
  return lines.join("\n");
}

function buildLedgerPrompt(
  prep: SessionBeforeCompactEvent["preparation"],
  extraInstructions: string | undefined,
): string {
  const sections: string[] = [];
  if (prep.previousSummary) {
    sections.push(
      "## Previous compaction summary (update it; carry forward still-live ledger lines)\n" +
        prep.previousSummary,
    );
  }
  sections.push(
    `## Transcript to summarize (${prep.messagesToSummarize.length} messages)\n` +
      digestMessages(prep.messagesToSummarize as Msg[], 24000),
  );
  if (prep.turnPrefixMessages?.length) {
    sections.push(
      `## Split-turn prefix (${prep.turnPrefixMessages.length} messages)\n` +
        digestMessages(prep.turnPrefixMessages as Msg[], 8000),
    );
  }
  if (extraInstructions) {
    sections.push(`## Extra custom instructions\n${extraInstructions}`);
  }
  sections.push(EVIDENCE_LEDGER_PROMPT);
  return sections.join("\n\n");
}

// ------------------------------------------------------------------- models

function pickSummarizer(ctx: ExtensionContext): Model<any> | undefined {
  const spec = process.env.GE_COMPACT_MODEL?.trim();
  if (spec) {
    const i = spec.indexOf("/");
    if (i > 0 && i < spec.length - 1) {
      const m = ctx.modelRegistry.find(spec.slice(0, i), spec.slice(i + 1));
      if (m) return m;
      try {
        ctx.ui.notify?.(`compaction-ledger: GE_COMPACT_MODEL "${spec}" not found; using session model`, "warning");
      } catch {
        /* headless */
      }
    }
  }
  return ctx.model;
}

function extractSummaryText(resp: unknown): string {
  const r = resp as { content?: unknown };
  if (Array.isArray(r?.content)) {
    return (r.content as Array<Record<string, unknown>>)
      .filter((p) => p?.type === "text")
      .map((p) => (p.text as string) ?? "")
      .join("");
  }
  return typeof r?.content === "string" ? (r.content as string) : "";
}

// -------------------------------------------------------------------- entry

export default function (pi: ExtensionAPI): void {
  pi.registerCommand("compact-ledger", {
    description: "Compact now with the evidence-ledger summary prompt (usage: /compact-ledger [extra instruction])",
    handler: async (args: string, ctx: ExtensionContext) => {
      const instructions = [
        EVIDENCE_LEDGER_PROMPT,
        args ? `Additional user instruction: ${args}` : "",
      ]
        .filter(Boolean)
        .join("\n");
      ctx.compact({
        customInstructions: instructions,
        onError: (err) => {
          try {
            ctx.ui.notify?.(`compaction failed: ${err.message}`, "error");
          } catch {
            /* headless */
          }
        },
      });
      try {
        ctx.ui.notify?.("Compaction started (evidence ledger)", "info");
      } catch {
        /* headless */
      }
    },
  });

  pi.on("session_before_compact", async (event, ctx): Promise<SessionBeforeCompactResult | undefined> => {
    if (process.env.GE_COMPACT_LEDGER === "0") return; // opt-out: built-in compaction
    const prep = event.preparation;
    const model = pickSummarizer(ctx);
    if (!model) return; // no model resolvable -> built-in compaction
    try {
      const prompt = buildLedgerPrompt(prep, event.customInstructions);
      const maxTokens = Math.min(8192, Math.max(2048, Math.floor(((model as { maxTokens?: number }).maxTokens ?? 4096) / 2)));
      const resp = await completeSimple(
        model as never,
        {
          systemPrompt: SUMMARY_SYSTEM,
          messages: [{ role: "user", content: prompt }],
        },
        { signal: event.signal, maxTokens, temperature: 0.2 },
      );
      const summary = extractSummaryText(resp);
      if (!summary.trim()) throw new Error("summarizer returned an empty summary");
      try {
        ctx.ui.notify?.(
          `Compaction complete (evidence ledger): ${prep.tokensBefore} -> ~${Math.ceil(summary.length / 4)} tokens`,
          "info",
        );
      } catch {
        /* headless */
      }
      return {
        compaction: {
          summary,
          firstKeptEntryId: prep.firstKeptEntryId,
          tokensBefore: prep.tokensBefore,
        },
      };
    } catch (err) {
      try {
        ctx.ui.notify?.(
          `compaction-ledger: custom summarization failed (${(err as Error).message}); using built-in compaction`,
          "warning",
        );
      } catch {
        /* headless */
      }
      return; // undefined -> pi's built-in compaction takes over
    }
  });
}
