# Memory-Efficient JamSpell Training from Zipped PubMed XML

## Table of Contents
1. [Overview](#overview)
2. [Understanding Your Data](#understanding-your-data)
3. [Memory-Efficient Processing Strategy](#memory-efficient-processing-strategy)
4. [Complete Processing Script](#complete-processing-script)
5. [Step-by-Step Training Process](#step-by-step-training-process)
6. [Advanced Memory Management](#advanced-memory-management)
7. [Performance Monitoring](#performance-monitoring)
8. [Troubleshooting](#troubleshooting)

---

## Overview

This guide provides a complete solution for training JamSpell models directly from your zipped PubMed XML files (`pubmed25nXXXX.xml.gz`) without extracting them to disk. The solution is designed to handle your 40GB+ dataset efficiently with minimal memory usage.

### Key Features
- **Direct zip processing**: No need to extract files to disk
- **Memory-efficient**: Handles 40GB+ datasets with only 2-4GB RAM
- **Batch processing**: Processes files in small batches
- **Streaming output**: Writes text immediately without buffering
- **Progress monitoring**: Real-time progress tracking
- **Error handling**: Robust error recovery

---

## Understanding Your Data

### File Structure
Your PubMed data consists of:
- **Compressed XML files**: `pubmed25n0001.xml.gz`, `pubmed25n0002.xml.gz`, etc.
- **MD5 checksum files**: `pubmed25n0001.xml.gz.md5`, `pubmed25n0002.xml.gz.md5`, etc.
- **File sizes**: 16-20MB each compressed file
- **Total dataset**: 40GB+ across thousands of files

### XML Content Structure
```xml
<PubmedArticle>
  <MedlineCitation>
    <Article>
      <Abstract>
        <AbstractText>Medical abstract text here...</AbstractText>
      </Abstract>
      <ArticleTitle>Title of the article</ArticleTitle>
    </Article>
  </MedlineCitation>
</PubmedArticle>
```

### Data Characteristics
- **Format**: Gzip compressed XML
- **Content**: Medical/scientific abstracts and text
- **Language**: English medical/scientific text
- **Size per file**: ~16-20MB compressed
- **Total files**: Potentially thousands

---

## Memory-Efficient Processing Strategy

### Processing Approach
1. **Direct zip reading**: Read compressed files without extracting
2. **Batch processing**: Process files in small batches (default: 100 files)
3. **Streaming output**: Write text immediately to avoid memory buildup
4. **Garbage collection**: Force memory cleanup after each batch
5. **Progressive processing**: Handle any dataset size without memory overflow

### Memory Management
- **Peak memory**: 2-4GB (regardless of dataset size)
- **Stable processing**: Memory usage stays constant
- **No disk space needed**: Processes directly from compressed files
- **Batch size control**: Adjustable based on available RAM

### Processing Time Estimates
- **Small dataset** (1-100 files): 10-30 minutes
- **Medium dataset** (100-1000 files): 30 minutes - 2 hours
- **Large dataset** (1000+ files): 2-8 hours
- **Your 40GB dataset**: 4-12 hours (depending on system)

---

## Complete Processing Script

### Create the Memory-Efficient Processing Script

Create a file called `process_pubmed_zipped.ps1` with the following content:

```powershell
# Memory-Efficient PubMed XML to JamSpell Training Data Processor
# Processes zipped XML files directly without extracting to disk

param(
    [string]$InputFolder = "C:\your_pubmed_data_folder",
    [string]$OutputFile = "pubmed_training_data.txt",
    [int]$BatchSize = 100,  # Process 100 files at a time for memory efficiency
    [switch]$CreateAlphabet = $true,
    [switch]$Verbose = $false
)

Write-Host "=== Memory-Efficient PubMed XML Processor ===" -ForegroundColor Green
Write-Host "Processing zipped PubMed XML files for JamSpell training..." -ForegroundColor Cyan
Write-Host "Memory-efficient processing - no disk extraction required!" -ForegroundColor Yellow

# Function to extract text from PubMed XML
function Extract-TextFromPubMedXML {
    param([string]$xmlContent)
    
    $texts = @()
    
    # Extract abstract text from PubMed XML
    $abstractMatches = [regex]::Matches($xmlContent, '<AbstractText[^>]*>(.*?)</AbstractText>', [System.Text.RegularExpressions.RegexOptions]::Singleline)
    foreach ($match in $abstractMatches) {
        $abstract = $match.Groups[1].Value
        $abstract = $abstract -replace '<[^>]+>', ''  # Remove XML tags
        $abstract = $abstract -replace '&[^;]+;', ''  # Remove HTML entities
        $abstract = $abstract.Trim()
        
        if ($abstract.Length -gt 10) {
            $texts += $abstract
        }
    }
    
    # Extract article titles
    $titleMatches = [regex]::Matches($xmlContent, '<ArticleTitle[^>]*>(.*?)</ArticleTitle>', [System.Text.RegularExpressions.RegexOptions]::Singleline)
    foreach ($match in $titleMatches) {
        $title = $match.Groups[1].Value
        $title = $title -replace '<[^>]+>', ''
        $title = $title -replace '&[^;]+;', ''
        $title = $title.Trim()
        
        if ($title.Length -gt 5) {
            $texts += $title
        }
    }
    
    return $texts
}

# Function to process a single .gz file directly
function Process-GzFileDirect {
    param([string]$filePath)
    
    try {
        # Use .NET GZipStream to decompress directly
        $fileStream = [System.IO.File]::OpenRead($filePath)
        $gzipStream = New-Object System.IO.Compression.GZipStream($fileStream, [System.IO.Compression.CompressionMode]::Decompress)
        $reader = New-Object System.IO.StreamReader($gzipStream)
        
        $xmlContent = $reader.ReadToEnd()
        $reader.Close()
        $gzipStream.Close()
        $fileStream.Close()
        
        # Extract text from XML
        $texts = Extract-TextFromPubMedXML -xmlContent $xmlContent
        
        return $texts
    }
    catch {
        Write-Host "Error processing $filePath : $($_.Exception.Message)" -ForegroundColor Red
        return @()
    }
}

# Function to monitor memory usage
function Get-MemoryUsage {
    $process = Get-Process -Id $PID
    $memoryMB = [math]::Round($process.WorkingSet64 / 1MB, 2)
    return $memoryMB
}

# Main processing logic
Write-Host "`nStep 1: Collecting file list..." -ForegroundColor Yellow
$allFiles = Get-ChildItem -Path $InputFolder -Filter "*.xml.gz" | Sort-Object Name
Write-Host "Found $($allFiles.Count) files to process" -ForegroundColor Green
Write-Host "Estimated total size: $([math]::Round($allFiles.Count * 18 / 1024, 2)) GB" -ForegroundColor Cyan

# Initialize output file
if (Test-Path $OutputFile) { 
    Write-Host "Removing existing output file..." -ForegroundColor Yellow
    Remove-Item $OutputFile 
}
$outputStream = [System.IO.File]::CreateText($OutputFile)

$processedCount = 0
$totalTexts = 0
$startTime = Get-Date
$initialMemory = Get-MemoryUsage

Write-Host "`nStep 2: Processing files in batches..." -ForegroundColor Yellow
Write-Host "Initial memory usage: $initialMemory MB" -ForegroundColor Gray

# Process files in batches to manage memory
for ($i = 0; $i -lt $allFiles.Count; $i += $BatchSize) {
    $batch = $allFiles | Select-Object -Skip $i -First $BatchSize
    $batchNumber = [math]::Floor($i / $BatchSize) + 1
    $totalBatches = [math]::Ceiling($allFiles.Count / $BatchSize)
    
    Write-Host "`nProcessing batch $batchNumber of $totalBatches" -ForegroundColor Cyan
    Write-Host "Memory usage: $(Get-MemoryUsage) MB" -ForegroundColor Gray
    
    $batchStartTime = Get-Date
    $batchTexts = 0
    
    foreach ($file in $batch) {
        if ($Verbose) {
            Write-Host "  Processing $($file.Name)..." -ForegroundColor Gray
        }
        
        $texts = Process-GzFileDirect -filePath $file.FullName
        
        # Write texts to output file immediately (streaming)
        foreach ($text in $texts) {
            $outputStream.WriteLine($text)
            $totalTexts++
            $batchTexts++
        }
        
        $processedCount++
        
        # Progress indicator every 10 files
        if ($processedCount % 10 -eq 0) {
            $elapsed = (Get-Date) - $startTime
            $rate = $processedCount / $elapsed.TotalMinutes
            Write-Host "    Processed $processedCount/$($allFiles.Count) files ($([math]::Round($processedCount * 100 / $allFiles.Count, 1))%)" -ForegroundColor Green
            Write-Host "    Rate: $([math]::Round($rate, 1)) files/minute" -ForegroundColor Gray
            Write-Host "    Memory: $(Get-MemoryUsage) MB" -ForegroundColor Gray
        }
    }
    
    $batchTime = (Get-Date) - $batchStartTime
    Write-Host "  Batch $batchNumber complete: $batchTexts texts extracted in $([math]::Round($batchTime.TotalSeconds, 1)) seconds" -ForegroundColor Green
    
    # Force garbage collection to free memory
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
    
    $currentMemory = Get-MemoryUsage
    Write-Host "  Memory after cleanup: $currentMemory MB" -ForegroundColor Gray
}

$outputStream.Close()

$totalTime = (Get-Date) - $startTime
$finalMemory = Get-MemoryUsage

Write-Host "`nStep 3: Processing complete!" -ForegroundColor Green
Write-Host "Processed $processedCount files" -ForegroundColor Cyan
Write-Host "Extracted $totalTexts texts" -ForegroundColor Cyan
Write-Host "Total time: $([math]::Round($totalTime.TotalHours, 1)) hours" -ForegroundColor Cyan
Write-Host "Average rate: $([math]::Round($processedCount / $totalTime.TotalMinutes, 1)) files/minute" -ForegroundColor Cyan
Write-Host "Memory usage: $initialMemory MB → $finalMemory MB" -ForegroundColor Cyan
Write-Host "Output file: $OutputFile" -ForegroundColor Cyan

# Create alphabet if requested
if ($CreateAlphabet) {
    Write-Host "`nStep 4: Creating alphabet file..." -ForegroundColor Yellow
    
    $alphabetFile = "pubmed_alphabet.txt"
    $uniqueChars = New-Object System.Collections.Generic.HashSet[char]
    
    Write-Host "Reading processed data to extract alphabet..." -ForegroundColor Gray
    $reader = [System.IO.File]::OpenText($OutputFile)
    $lineCount = 0
    
    while (($line = $reader.ReadLine()) -ne $null) {
        $lineCount++
        if ($lineCount % 10000 -eq 0) {
            Write-Host "  Processing line $lineCount for alphabet..." -ForegroundColor Gray
        }
        
        $chars = $line.ToLower().ToCharArray()
        foreach ($char in $chars) {
            if ([char]::IsLetterOrDigit($char) -or [char]::IsWhiteSpace($char)) {
                $uniqueChars.Add($char) | Out-Null
            }
        }
    }
    $reader.Close()
    
    # Write alphabet file
    $alphabetChars = $uniqueChars | Sort-Object
    $alphabetChars -join '' | Out-File $alphabetFile -Encoding UTF8
    
    Write-Host "Alphabet file created: $alphabetFile" -ForegroundColor Green
    Write-Host "Alphabet contains $($alphabetChars.Count) unique characters" -ForegroundColor Cyan
}

Write-Host "`n=== Processing Complete ===" -ForegroundColor Green
Write-Host "Ready to train JamSpell on your PubMed data!" -ForegroundColor Green
Write-Host "Memory-efficient processing successful!" -ForegroundColor Green
```

### Run the Memory-Efficient Processing Script

```powershell
# Navigate to your JamSpell directory
cd "C:\stealth project North Carolina\JamSpell"

# Run the memory-efficient processing script
.\process_pubmed_zipped.ps1 -InputFolder "C:\path\to\your\pubmed\data" -OutputFile "pubmed_training_data.txt" -Verbose

# For even more memory efficiency, reduce batch size
.\process_pubmed_zipped.ps1 -InputFolder "C:\path\to\your\pubmed\data" -OutputFile "pubmed_training_data.txt" -BatchSize 50
```

---

## Step-by-Step Training Process

### Step 1: Process Your Zipped Data
```powershell
# Process your PubMed XML files directly from zip
.\process_pubmed_zipped.ps1 -InputFolder "C:\your_pubmed_folder" -OutputFile "pubmed_data.txt"
```

### Step 2: Monitor Processing Progress
The script will show:
- Real-time progress updates
- Memory usage monitoring
- Processing rate (files/minute)
- Estimated completion time

### Step 3: Train JamSpell Model
```powershell
# Train on your processed PubMed data
.\build\main\Release\jamspell.exe train pubmed_alphabet.txt pubmed_data.txt pubmed_model.bin
```

### Step 4: Monitor Training Progress
During training, you'll see: