import test from "node:test";
import assert from "node:assert/strict";
import { resolveClientProfile } from "./clientProfile.js";
test("resolves codex from name", () => {
    assert.equal(resolveClientProfile({ name: "Codex", version: "1.0.0" }), "codex");
});
test("resolves codex from name and desktop version", () => {
    assert.equal(resolveClientProfile({ name: "OpenAI Codex", version: "desktop" }), "codex");
});
test("resolves codex from version only", () => {
    assert.equal(resolveClientProfile({ name: "Cursor", version: "Codex desktop" }), "codex");
});
test("resolves claude from name", () => {
    assert.equal(resolveClientProfile({ name: "Claude Desktop", version: "1.0.0" }), "claude");
});
test("resolves claude from name and beta version", () => {
    assert.equal(resolveClientProfile({ name: "Claude Code", version: "beta" }), "claude");
});
test("resolves claude from version only", () => {
    assert.equal(resolveClientProfile({ name: "Cursor", version: "Claude desktop" }), "claude");
});
test("resolves generic when client info is missing", () => {
    assert.equal(resolveClientProfile(undefined), "generic");
});
test("resolves generic for unknown client", () => {
    assert.equal(resolveClientProfile({ name: "Cursor", version: "1.0.0" }), "generic");
});
