# Viewport Screenshot Context Feature

**Date:** 2026-03-02
**Status:** Approved

## Overview

Add `/screenshot` quick command to the AI Chat Window that captures the Level Editor Viewport and attaches it as a multimodal image to the AI request.

## Design

### Data Flow

1. User types `/screenshot <text>` in chat input
2. `ParseQuickCommand` extracts command=`"screenshot"`, args=`<text>`
3. `ExecuteQuickCommand` captures Level Viewport → JPEG → base64 data URL
4. Creates `FChatMessage` with `ImageDataUrls` populated
5. `BuildRequestBody` serializes content as OpenAI multimodal array
6. AI receives both text + image

### File Changes

| File | Change |
|------|--------|
| `OpenAICompatibleService.h` | Add `TArray<FString> ImageDataUrls` to `FChatMessage` |
| `OpenAICompatibleService.cpp` | `BuildRequestBody` serializes multimodal content when images present |
| `ConversationContext.h` | Add `AddUserMessageWithImages()` |
| `ConversationContext.cpp` | Implement new method |
| `SAIChatWindow.h` | Add `bHasScreenshot` to `FChatDisplayMessage`, add `CaptureViewportAsBase64()` |
| `SAIChatWindow.cpp` | Handle `/screenshot` command, show "[截图已附加]" in UI |
| `AssetFactoryEditor.Build.cs` | Add `ImageWrapper` dependency |

### Multimodal JSON Format

```json
{
  "role": "user",
  "content": [
    {"type": "text", "text": "Tell me about the scene"},
    {"type": "image_url", "image_url": {"url": "data:image/jpeg;base64,..."}}
  ]
}
```

### Backwards Compatibility

When `ImageDataUrls` is empty, `BuildRequestBody` serializes `content` as plain string (existing behavior).
