import type { Implementation } from "@modelcontextprotocol/sdk/types.js";
export type ClientProfile = "codex" | "claude" | "generic";
export declare function resolveClientProfile(clientInfo?: Implementation): ClientProfile;
