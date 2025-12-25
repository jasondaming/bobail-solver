# Bobail Solver

A complete game solver for Bobail, an abstract strategy board game from Madagascar. This project implements retrograde analysis to perfectly solve the game and provides a web interface to play against optimal AI.

**Play online:** [https://jasondaming.github.io/bobail-solver/](https://jasondaming.github.io/bobail-solver/)

## Game Overview

Bobail is played on a 5x5 board between two players (Green and Red). Each player has 5 pawns, and there is a shared piece called the Bobail.

### Win Conditions
- **Green wins** if the Bobail reaches row 0 (Green's home row)
- **Red wins** if the Bobail reaches row 4 (Red's home row)
- **Draw** occurs on 3-fold repetition

### Rules Variants

The solver supports two rule variants:

1. **Official Rules** (Board Game Arena rules): Pawns must move their maximum possible distance in a straight line
2. **Flexible Rules**: Pawns can stop at any square along their movement path

### Turn Structure
1. Move the Bobail one square orthogonally (up, down, left, right)
2. Move one of your pawns in a straight line (orthogonally or diagonally)

In Official rules, the first turn of the game skips the Bobail movement.

## Project Components

### Solver (`src/retrograde_db.cpp`)
The core retrograde analysis solver that:
- Enumerates all legal game positions
- Works backwards from terminal positions (wins/losses)
- Propagates results until all positions are classified
- Uses RocksDB for persistent storage
- Supports multi-threaded parallel processing

### Web Interface (`docs/`)
A browser-based game interface featuring:
- Play against AI with multiple difficulty levels (Easy, Medium, Hard, Perfect)
- Two-player local mode
- Setup mode for custom positions
- Game replay with move navigation
- Position sharing via URL
- Sound effects and animations
- Mobile-responsive design

### Command-Line Tools

#### `retrograde_db_main` - Run the solver
```bash
./build/retrograde_db_main --db ./solver_db --threads 8 --official
```

Options:
- `--db PATH`: Database directory (required)
- `--threads N`: Number of parallel threads
- `--official`: Use Official rules (default)
- `--flexible`: Use Flexible rules
- `--import FILE`: Import from checkpoint file

#### `lookup` - Query solved positions
```bash
./build/lookup --db ./solver_db --official --interactive
```

Query format: `WP,BP,BOB,STM` (white pawns hex, black pawns hex, bobail square, side to move)

#### `export_book` - Export opening book to JSON
```bash
./build/export_book --db ./solver_db --output opening_book.json --depth 20
```

#### `lookup_server.py` - HTTP API server
```bash
python src/lookup_server.py --db ./solver_db --rules official --port 8080
```

Provides REST API for the web interface to query the solved database.

## Building

### Prerequisites
- C++17 compiler (GCC, Clang, or MSVC)
- CMake 3.14+
- RocksDB library
- Ninja (recommended) or Make

### Using Docker (Recommended)
```bash
docker build -t bobail-solver .
docker run -v $(pwd)/solver_db:/data bobail-solver --db /data --threads 8
```

### Manual Build
```bash
cmake -B build -G Ninja
cmake --build build
```

### Running Tests
```bash
./build/bobail_tests
```

## Results

With perfect play from the starting position:
- **Official Rules**: The game is a **DRAW**
- **Flexible Rules**: The game is a **DRAW**

The solver has computed optimal moves for all reachable positions in both variants.

## Architecture

```
bobail-solver/
├── include/           # Header files
│   ├── board.h       # Board representation
│   ├── movegen.h     # Move generation
│   ├── hash.h        # Zobrist hashing
│   ├── symmetry.h    # Position canonicalization
│   └── retrograde_db.h  # Solver database interface
├── src/              # Source files
│   ├── movegen.cpp   # Move generation implementation
│   ├── symmetry.cpp  # Symmetry transformations
│   ├── retrograde_db.cpp      # Main solver implementation
│   ├── retrograde_db_main.cpp # Solver CLI
│   ├── lookup.cpp    # Position lookup tool
│   └── export_book.cpp  # Opening book exporter
├── docs/             # Web interface (GitHub Pages)
│   ├── index.html    # Main HTML
│   ├── game.js       # Game logic and UI
│   └── style.css     # Styling
└── tests/            # Unit tests
```

## Position Encoding

Positions are encoded as a 64-bit integer:
- Bits 0-24: White pawn positions (one bit per square)
- Bits 25-49: Black pawn positions
- Bits 50-54: Bobail square (0-24)
- Bit 55: Side to move

Symmetry reduction uses horizontal flip only (to preserve goal row semantics).

## Performance

The solver processes millions of positions per second using:
- Bitboard representation for fast move generation
- Pre-computed move tables
- Lock-free concurrent queue for parallel processing
- RocksDB batch writes for efficient I/O
- Position canonicalization to reduce search space by ~2x

## License

MIT License

## Acknowledgments

- Bobail is a traditional Malagasy game
- RocksDB by Facebook/Meta for persistent storage
- Google Test for unit testing framework
