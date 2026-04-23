import type { ClientProfile } from "./clientProfile.js";
export type ToolDefinition = {
    name: string;
    description?: string;
};
export declare const ALL_TOOLS: ToolDefinition[];
export declare function buildVisibleTools<T extends ToolDefinition>(allTools: T[], profile: ClientProfile): T[];
export declare function assertToolAllowed(profile: ClientProfile, toolName: string): void;
export declare function validateToolCatalogPolicy(allToolNames: string[]): void;
