# JamSpell Test Script
Write-Host "=== JamSpell Spell Checker Test ===" -ForegroundColor Green

# Test 1: Command line spell correction
Write-Host "`n1. Testing command line spell correction:" -ForegroundColor Yellow
$testPhrases = @(
    "I am the begt spell cherken",
    "helo world", 
    "wonderful day",
    "this is a testt"
)

foreach ($phrase in $testPhrases) {
    Write-Host "Input: $phrase"
    $result = echo $phrase | .\build\main\Release\jamspell.exe correct model_test.bin
    Write-Host "Output: $result`n"
}

# Test 2: Web server spell correction
Write-Host "`n2. Testing web server spell correction:" -ForegroundColor Yellow
$webTests = @(
    "I am the begt spell cherken",
    "helo world",
    "wonderful day"
)

foreach ($phrase in $webTests) {
    Write-Host "Input: $phrase"
    try {
        $response = Invoke-WebRequest -Uri "http://localhost:8080/fix?text=$phrase" -UseBasicParsing
        Write-Host "Output: $($response.Content)`n"
    } catch {
        Write-Host "Error: $($_.Exception.Message)`n"
    }
}

Write-Host "=== Test Complete ===" -ForegroundColor Green 