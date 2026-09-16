# 🧠 Fly Brain AI Player — Persistent Learning Edition

The AI **remembers everything** — straight flying sections, successful jump timings, safe patterns — and gets SMARTER the more it plays!

### ✨ What's New
- 💾 **Persistent Memory**: Saves learning to disk — survives game restarts & mod updates
- ✈️ **Straight Flying Detection**: Recognizes open sections and automatically holds steady
- 🧫 **Adaptive Weights**: Neurons strengthen/reinforce patterns that work
- 📊 **Per-Level Learning**: Each level has its own saved brain file
- ⏸️ **5-Second Resume Hold**: Prevent accidental unpausing

### How It Learns
- ✅ Completes a section → strengthens that pattern → more likely to repeat it
- ❌ Fails → adjusts weights → avoids repeating the same mistake
- 📈 Confidence grows with success rate
- 💾 Auto-saves after every attempt

### Build
```bash
geode build
