# StateTree Property Bindings Deferred Polish

- Task 2 code quality review minor: move duplicate state GUID validation in `RegisterStateReference()` before `ByPath.Add()` so the function remains transactional on failure.
- Task 2 code quality review minor: keep an eye on `StateNodeById` as the generic state-local `kind: node` index. Typed endpoints now bypass it, but future changes should avoid making it a broad fallback again.
- Task 4 code quality review minor: extracted property-function bindings are intentionally skipped until the function-binding task lands.
- Task 4 code quality review minor: omit or support extracted `instanceStruct` metadata once the generator has a concrete consumer for it.
- Task 4 code quality review minor: sort extracted binding output for stable diffs.
