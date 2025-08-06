# JamSpell Quick Start Guide

## 🚀 Quick Commands

### Build JamSpell
```powershell
cd "C:\stealth project North Carolina\JamSpell"
mkdir build; cd build
cmake ..
cmake --build . --config Release
```

### Train a Model
```powershell
# Basic training
.\build\main\Release\jamspell.exe train test_data\alphabet_en.txt your_data.txt model.bin

# Train on sample data (for testing)
.\build\main\Release\jamspell.exe train test_data\alphabet_en.txt test_data\sherlockholmes.txt model_test.bin
```

### Use Spell Correction

#### Command Line
```powershell
# Interactive mode
.\build\main\Release\jamspell.exe correct model.bin

# Pipe text
echo "I am the begt spell cherken" | .\build\main\Release\jamspell.exe correct model.bin

# Fix a file
.\build\main\Release\jamspell.exe fix model.bin input.txt output.txt
```

#### Web Server
```powershell
# Start server
.\build\web_server\Release\web_server.exe model.bin localhost 8080

# Test via HTTP
Invoke-WebRequest -Uri "http://localhost:8080/fix?text=I am the begt spell cherken"
```

## 📁 File Structure
```
JamSpell/
├── build/main/Release/jamspell.exe      # Main executable
├── build/web_server/Release/web_server.exe  # Web server
├── test_data/alphabet_en.txt            # English alphabet
├── test_data/sherlockholmes.txt         # Sample training data
├── model_test.bin                       # Trained model
└── JAMSPELL_TRAINING_GUIDE.md          # Detailed guide
```

## 🎯 For Your 40GB Data

### Step 1: Prepare Your Data
```powershell
# Combine your text files
Get-ChildItem -Path "your_data_folder" -Filter "*.txt" | 
    ForEach-Object { Get-Content $_.FullName } | 
    Out-File combined_data.txt -Encoding UTF8
```

### Step 2: Create Alphabet
```powershell
# Extract unique characters
Get-Content combined_data.txt | 
    ForEach-Object { $_.ToLower() } | 
    ForEach-Object { $_.ToCharArray() } | 
    Sort-Object -Unique | 
    Out-File custom_alphabet.txt -Encoding UTF8
```

### Step 3: Train Model
```powershell
.\build\main\Release\jamspell.exe train custom_alphabet.txt combined_data.txt your_model.bin
```

### Step 4: Test Your Model
```powershell
echo "your text with typos" | .\build\main\Release\jamspell.exe correct your_model.bin
```

## 🔧 Troubleshooting

| Problem | Solution |
|---------|----------|
| `cmake not found` | `pip install cmake` |
| `failed to load model` | Check model file exists |
| Web server not responding | `netstat -an \| findstr 8080` |
| Poor corrections | Train on more/better data |

## 📊 Model Performance

| Data Size | Training Time | Accuracy |
|-----------|---------------|----------|
| 1-10MB | 1-5 min | Basic |
| 10-100MB | 5-30 min | Good |
| 100MB+ | 30+ min | Excellent |

## 🎯 Next Steps

1. **Train on your 40GB data** using the steps above
2. **Test with your domain-specific text**
3. **Deploy web server** for production use
4. **Monitor and improve** based on results

---

**Status**: ✅ Ready to train on your 40GB dataset! 