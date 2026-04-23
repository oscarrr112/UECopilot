export function resolveClientProfile(clientInfo) {
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
