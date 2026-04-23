import type { Implementation } from "@modelcontextprotocol/sdk/types.js";

export type ClientProfile = "codex" | "claude" | "generic";

export function resolveClientProfile(clientInfo?: Implementation): ClientProfile {
  if (!clientInfo) {
    return "generic";
  }

  const haystack = `${clientInfo.name ?? ""} ${clientInfo.version ?? ""}`.toLowerCase();

  if (haystack.includes("codex")) {
    return "codex";
  }

  if (haystack.includes("claude")) {
    return "claude";
  }

  return "generic";
}
