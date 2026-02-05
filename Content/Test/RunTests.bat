@echo off
REM AssetFactory Commandlet Test Runner
REM Usage: Run from the project directory or specify UE_PROJECT_PATH

SET UE_EDITOR="C:\Program Files\Epic Games\UE_5.5\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
SET PROJECT_PATH=%~dp0..\..\..\..\..\..\ProjectRPG.uproject

echo ==========================================
echo AssetFactory Test Runner
echo ==========================================
echo.

REM Test 1: Run refactoring validation test (should all pass)
echo [Test 1] Running Refactoring Validation Test...
%UE_EDITOR% %PROJECT_PATH% -run=AssetFactory -json="%~dp0RefactoringValidationTest.json" -verbose
echo.

REM Test 2: Run validation failure test (should all fail with clear error messages)
echo [Test 2] Running Validation Failure Test (expected to show errors)...
%UE_EDITOR% %PROJECT_PATH% -run=AssetFactory -json="%~dp0ValidationFailureTest.json" -verbose
echo.

REM Test 3: Run existing basic test
echo [Test 3] Running Basic Widget Test...
%UE_EDITOR% %PROJECT_PATH% -run=AssetFactory -json="%~dp0WBP_BasicTest.json" -verbose
echo.

REM Test 4: Run existing TestAssets.json
echo [Test 4] Running TestAssets...
%UE_EDITOR% %PROJECT_PATH% -run=AssetFactory -json="%~dp0..\..\..\TestAssets.json" -verbose
echo.

echo ==========================================
echo Tests Complete
echo ==========================================
pause
