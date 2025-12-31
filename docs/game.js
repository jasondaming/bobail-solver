// Bobail Game Logic

const BOARD_SIZE = 5;
const NUM_SQUARES = 25;

// Directions for movement (row, col deltas)
const DIRECTIONS = [
    [-1, 0],  // North
    [1, 0],   // South
    [0, 1],   // East
    [0, -1],  // West
    [-1, 1],  // NE
    [-1, -1], // NW
    [1, 1],   // SE
    [1, -1]   // SW
];

// Difficulty settings (search depth, or 'perfect' for solver)
const DIFFICULTY = {
    easy: 1,
    medium: 3,
    hard: 5,
    impossible: 'perfect'  // Uses perfect solver database
};

// Stats tracking
let stats = {
    wins: 0,
    losses: 0,
    draws: 0
};

// Game history - stores completed games
let gameHistory = [];

// Hint system state (declared early for use in makeMove)
let pendingHints = new Map();
let hintRequestPending = false;
let currentPositionEval = null;

// Sound effects
const sounds = {
    move: null,
    capture: null,
    gameOver: null,
    enabled: true
};

// Game state
let gameState = {
    greenPawns: [],      // Array of square indices (first player)
    redPawns: [],        // Array of square indices (second player)
    bobailSquare: 12,    // Center square
    greenToMove: true,   // Green moves first
    phase: 'bobail',     // 'bobail' or 'pawn'
    selectedSquare: null,
    validMoves: [],
    moveHistory: [],
    hintsEnabled: false,
    playerColor: 'green', // 'green', 'red', or 'both'
    gameOver: false,
    winner: null,        // 'green', 'red', or null
    difficulty: 'medium',
    animating: false,
    setupMode: false,         // Board setup mode
    setupPiece: 'green',      // Which piece type to place: 'green', 'red', 'bobail', 'clear'
    aiThinking: false,        // AI is computing a move
    replayMode: false,        // Viewing historical position
    replayIndex: 0            // Current position in replay (0 = start, moveHistory.length = current)
};

// Load stats from localStorage
function loadStats() {
    const saved = localStorage.getItem('bobail-stats');
    if (saved) {
        stats = JSON.parse(saved);
    }
}

// Save stats to localStorage
function saveStats() {
    localStorage.setItem('bobail-stats', JSON.stringify(stats));
}

// Load game history from localStorage
function loadGameHistory() {
    const saved = localStorage.getItem('bobail-game-history');
    if (saved) {
        gameHistory = JSON.parse(saved);
    }
}

// Save game history to localStorage
function saveGameHistory() {
    // Keep only the last 20 games
    if (gameHistory.length > 20) {
        gameHistory = gameHistory.slice(-20);
    }
    localStorage.setItem('bobail-game-history', JSON.stringify(gameHistory));
}

// Save current game to history
function saveGameToHistory() {
    if (gameState.moveHistory.length === 0) return;

    const game = {
        id: Date.now(),
        date: new Date().toLocaleDateString(),
        time: new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
        moves: [...gameState.moveHistory],
        winner: gameState.winner,
        playerColor: gameState.playerColor,
        moveCount: Math.ceil(gameState.moveHistory.length / 2)
    };

    gameHistory.push(game);
    saveGameHistory();
    updateGameHistoryDisplay();
}

// Load a saved game for review
function loadGameForReview(gameId) {
    const game = gameHistory.find(g => g.id === gameId);
    if (!game) return;

    // Reset to starting position
    gameState.greenPawns = [0, 1, 2, 3, 4];
    gameState.redPawns = [20, 21, 22, 23, 24];
    gameState.bobailSquare = 12;
    gameState.greenToMove = true;
    gameState.phase = 'pawn';
    gameState.moveHistory = [...game.moves];
    gameState.gameOver = game.winner !== null;
    gameState.winner = game.winner;
    gameState.playerColor = 'both'; // Review mode
    gameState.selectedSquare = null;
    gameState.validMoves = [];
    gameState.replayMode = true;
    gameState.replayIndex = 0;
    gameState.isFirstMove = false;

    hideGameOverOverlay();
    renderBoard();
    updateUI();
    showToast(`Loaded game from ${game.date}`);
}

// Delete a game from history
function deleteGameFromHistory(gameId) {
    gameHistory = gameHistory.filter(g => g.id !== gameId);
    saveGameHistory();
    updateGameHistoryDisplay();
}

// Update game history display
function updateGameHistoryDisplay() {
    const historyList = document.getElementById('game-history-list');
    if (!historyList) return;

    historyList.innerHTML = '';

    if (gameHistory.length === 0) {
        historyList.innerHTML = '<div class="no-games">No saved games</div>';
        return;
    }

    // Show games in reverse order (newest first)
    [...gameHistory].reverse().forEach(game => {
        const entry = document.createElement('div');
        entry.className = 'game-history-entry';

        const resultClass = game.winner === game.playerColor ? 'win' :
                           game.winner && game.winner !== game.playerColor ? 'loss' : 'draw';
        const resultText = game.winner === 'green' ? 'G wins' :
                          game.winner === 'red' ? 'R wins' : 'Draw';

        entry.innerHTML = `
            <div class="game-info-row">
                <span class="game-date">${game.date} ${game.time}</span>
                <span class="game-result ${resultClass}">${resultText}</span>
            </div>
            <div class="game-details">
                ${game.moveCount} moves
            </div>
            <div class="game-actions">
                <button class="btn btn-small" onclick="loadGameForReview(${game.id})">Review</button>
                <button class="btn btn-small btn-secondary" onclick="deleteGameFromHistory(${game.id})">Delete</button>
            </div>
        `;

        historyList.appendChild(entry);
    });
}

// Initialize sound effects
function initSounds() {
    // Create audio context for generating sounds
    try {
        const AudioContext = window.AudioContext || window.webkitAudioContext;
        const audioCtx = new AudioContext();

        sounds.play = (type) => {
            if (!sounds.enabled) return;

            const oscillator = audioCtx.createOscillator();
            const gainNode = audioCtx.createGain();

            oscillator.connect(gainNode);
            gainNode.connect(audioCtx.destination);

            if (type === 'move') {
                oscillator.frequency.value = 440;
                gainNode.gain.setValueAtTime(0.1, audioCtx.currentTime);
                gainNode.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.1);
                oscillator.start(audioCtx.currentTime);
                oscillator.stop(audioCtx.currentTime + 0.1);
            } else if (type === 'gameOver') {
                oscillator.frequency.value = 523.25;
                gainNode.gain.setValueAtTime(0.2, audioCtx.currentTime);
                gainNode.gain.exponentialRampToValueAtTime(0.01, audioCtx.currentTime + 0.5);
                oscillator.start(audioCtx.currentTime);
                oscillator.stop(audioCtx.currentTime + 0.5);
            }
        };
    } catch (e) {
        sounds.play = () => {}; // No-op if audio not available
    }
}

// Initialize starting position
function initGame() {
    // First player always skips bobail move on turn 1 (core Bobail rule)
    gameState = {
        greenPawns: [0, 1, 2, 3, 4],      // Row 0 (Green's home row)
        redPawns: [20, 21, 22, 23, 24], // Row 4 (Red's home row)
        bobailSquare: 12,
        greenToMove: true,
        phase: 'pawn', // First turn always skips bobail move
        selectedSquare: null,
        validMoves: [],
        moveHistory: [],
        hintsEnabled: gameState.hintsEnabled,
        playerColor: gameState.playerColor,
        gameOver: false,
        winner: null,
        difficulty: gameState.difficulty,
        animating: false,
        isFirstMove: true,  // Track if this is the very first move
        replayMode: false,
        replayIndex: 0
    };
    // Auto-select bobail if it's player's turn in bobail phase
    if (!gameState.gameOver && gameState.phase === 'bobail' && !shouldAIMove()) {
        gameState.selectedSquare = gameState.bobailSquare;
        gameState.validMoves = getBobailMoves();
    }

    // Clear pending hints from previous game
    pendingHints.clear();

    renderBoard();
    updateUI();

    // Request hints if enabled and player's turn
    if (gameState.hintsEnabled && !shouldAIMove()) {
        requestHints();
    }
    updateStatsDisplay();

    // If playing as Red, AI (Green) moves first
    if (shouldAIMove()) {
        setTimeout(makeAIMove, 500);
    }
}

// Convert between row/col and square index
function toSquare(row, col) {
    return row * BOARD_SIZE + col;
}

function toRowCol(sq) {
    return [Math.floor(sq / BOARD_SIZE), sq % BOARD_SIZE];
}

// Check if square is valid
function isValidSquare(row, col) {
    return row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE;
}

// Get piece at square
function getPieceAt(sq) {
    if (sq === gameState.bobailSquare) return 'bobail';
    if (gameState.greenPawns.includes(sq)) return 'green';
    if (gameState.redPawns.includes(sq)) return 'red';
    return null;
}

// Check if square is occupied
function isOccupied(sq) {
    return getPieceAt(sq) !== null;
}

// Generate valid moves for Bobail (one step in any direction)
function getBobailMoves() {
    const moves = [];
    const [row, col] = toRowCol(gameState.bobailSquare);

    for (const [dr, dc] of DIRECTIONS) {
        const newRow = row + dr;
        const newCol = col + dc;
        if (isValidSquare(newRow, newCol)) {
            const newSq = toSquare(newRow, newCol);
            if (!isOccupied(newSq)) {
                moves.push(newSq);
            }
        }
    }
    return moves;
}

// Generate valid moves for a pawn (sliding in any direction)
function getPawnMoves(sq) {
    const moves = [];
    const [row, col] = toRowCol(sq);

    for (const [dr, dc] of DIRECTIONS) {
        let newRow = row + dr;
        let newCol = col + dc;
        let lastValidSq = null;

        while (isValidSquare(newRow, newCol)) {
            const newSq = toSquare(newRow, newCol);
            if (isOccupied(newSq)) break;

            // Official rules: must move as far as possible
            lastValidSq = newSq;
            newRow += dr;
            newCol += dc;
        }

        if (lastValidSq !== null) {
            moves.push(lastValidSq);
        }
    }
    return moves;
}

// Get pawn moves from a specific square using a custom state (for hint evaluation)
function getPawnMovesFrom(sq, state) {
    const moves = [];
    const [row, col] = toRowCol(sq);

    // Build occupied set from the custom state
    const occupied = new Set([
        ...state.greenPawns,
        ...state.redPawns,
        state.bobailSquare
    ]);

    for (const [dr, dc] of DIRECTIONS) {
        let newRow = row + dr;
        let newCol = col + dc;
        let lastValidSq = null;

        while (isValidSquare(newRow, newCol)) {
            const newSq = toSquare(newRow, newCol);
            if (occupied.has(newSq)) break;

            // Official rules: must move as far as possible
            lastValidSq = newSq;
            newRow += dr;
            newCol += dc;
        }

        if (lastValidSq !== null) {
            moves.push(lastValidSq);
        }
    }
    return moves;
}

// Get all valid moves for current phase
function getValidMoves() {
    if (gameState.gameOver) return [];

    if (gameState.phase === 'bobail') {
        return getBobailMoves();
    } else {
        // Pawn phase - need to select a pawn first
        if (gameState.selectedSquare === null) return [];
        return getPawnMoves(gameState.selectedSquare);
    }
}

// Get all pieces that can move in pawn phase
function getMovablePawns() {
    const pawns = gameState.greenToMove ? gameState.greenPawns : gameState.redPawns;
    return pawns.filter(sq => getPawnMoves(sq).length > 0);
}

// Check terminal conditions
function checkGameOver() {
    const bobailRow = Math.floor(gameState.bobailSquare / BOARD_SIZE);

    // Bobail on Green's home row (row 0) = Green wins
    if (bobailRow === 0) {
        return { over: true, winner: 'green' };
    }

    // Bobail on Red's home row (row 4) = Red wins
    if (bobailRow === BOARD_SIZE - 1) {
        return { over: true, winner: 'red' };
    }

    // Check if Bobail is trapped (can't move)
    // This happens at the start of a turn (bobail phase)
    // The player who TRAPPED the bobail wins (the opponent of current player)
    if (gameState.phase === 'bobail' && getBobailMoves().length === 0) {
        // Opponent wins because they trapped the bobail
        return { over: true, winner: gameState.greenToMove ? 'red' : 'green' };
    }

    return { over: false, winner: null };
}

// Animate a piece moving from one square to another
function animateMove(fromSq, toSq, pieceType, callback) {
    const board = document.getElementById('board');
    const squares = board.querySelectorAll('.square');
    const fromEl = squares[fromSq];
    const toEl = squares[toSq];

    if (!fromEl || !toEl) {
        callback();
        return;
    }

    const piece = fromEl.querySelector('.piece');
    if (!piece) {
        callback();
        return;
    }

    gameState.animating = true;

    // Get positions
    const fromRect = fromEl.getBoundingClientRect();
    const toRect = toEl.getBoundingClientRect();
    const boardRect = board.getBoundingClientRect();

    // Create animated piece
    const animPiece = piece.cloneNode(true);
    animPiece.classList.add('animating');
    animPiece.style.position = 'absolute';
    const pieceSize = piece.offsetWidth / 2;
    animPiece.style.left = (fromRect.left - boardRect.left + fromRect.width / 2 - pieceSize) + 'px';
    animPiece.style.top = (fromRect.top - boardRect.top + fromRect.height / 2 - pieceSize) + 'px';
    animPiece.style.zIndex = '100';
    animPiece.style.transition = 'left 0.25s ease, top 0.25s ease';

    // Hide original piece
    piece.style.opacity = '0';

    board.style.position = 'relative';
    board.appendChild(animPiece);

    // Trigger animation
    requestAnimationFrame(() => {
        animPiece.style.left = (toRect.left - boardRect.left + toRect.width / 2 - pieceSize) + 'px';
        animPiece.style.top = (toRect.top - boardRect.top + toRect.height / 2 - pieceSize) + 'px';
    });

    // Clean up after animation
    setTimeout(() => {
        animPiece.remove();
        gameState.animating = false;
        if (sounds.play) sounds.play('move');
        callback();
    }, 260);
}

// Make a move
function makeMove(toSquare, animate = true) {
    const fromSquare = gameState.phase === 'bobail' ? gameState.bobailSquare : gameState.selectedSquare;
    const pieceType = gameState.phase === 'bobail' ? 'bobail' : (gameState.greenToMove ? 'green' : 'red');

    // Clear hints when making a move - they'll be refreshed when needed
    pendingHints.clear();
    currentPositionEval = null;
    resetHintLegend();

    const doMove = () => {
        if (gameState.phase === 'bobail') {
            // Move bobail
            gameState.bobailSquare = toSquare;
            gameState.moveHistory.push({
                type: 'bobail',
                from: fromSquare,
                to: toSquare,
                greenToMove: gameState.greenToMove
            });
            gameState.phase = 'pawn';
            gameState.selectedSquare = null;
            gameState.validMoves = [];
        } else {
            // Move pawn
            const pawns = gameState.greenToMove ? gameState.greenPawns : gameState.redPawns;
            const idx = pawns.indexOf(fromSquare);
            pawns[idx] = toSquare;

            gameState.moveHistory.push({
                type: 'pawn',
                from: fromSquare,
                to: toSquare,
                greenToMove: gameState.greenToMove
            });

            // End turn
            gameState.greenToMove = !gameState.greenToMove;
            gameState.phase = 'bobail';
            gameState.selectedSquare = null;
            gameState.validMoves = [];
            gameState.isFirstMove = false;  // First move is complete
        }

        // Check for game over
        const result = checkGameOver();
        if (result.over) {
            gameState.gameOver = true;
            gameState.winner = result.winner;
            if (sounds.play) sounds.play('gameOver');
            updateStats(result.winner);
        }

        // Auto-select bobail at start of player's turn (bobail phase)
        if (!gameState.gameOver && gameState.phase === 'bobail' && !shouldAIMove()) {
            gameState.selectedSquare = gameState.bobailSquare;
            gameState.validMoves = getBobailMoves();
        }

        renderBoard();
        updateUI();

        // Request hints for auto-selected bobail
        if (!gameState.gameOver && gameState.phase === 'bobail' && !shouldAIMove() && gameState.hintsEnabled) {
            requestHints();
        }

        // AI move if needed - but only after a complete turn (back to bobail phase)
        // This prevents double-triggering when AI is mid-turn
        if (!gameState.gameOver && gameState.phase === 'bobail' && shouldAIMove()) {
            setTimeout(makeAIMove, 400);
        }
    };

    if (animate && !gameState.animating) {
        animateMove(fromSquare, toSquare, pieceType, doMove);
    } else {
        doMove();
    }
}

// Update stats after game over
function updateStats(winner) {
    // Save game to history (for all game types)
    saveGameToHistory();

    if (gameState.playerColor === 'both') return;

    const playerWon = (gameState.playerColor === winner);
    const playerLost = (gameState.playerColor !== winner);

    if (playerWon) {
        stats.wins++;
    } else if (playerLost) {
        stats.losses++;
    }
    saveStats();
    updateStatsDisplay();
}

// Update stats display
function updateStatsDisplay() {
    const statsEl = document.getElementById('stats-display');
    if (statsEl) {
        statsEl.innerHTML = `<span class="stat-win">W: ${stats.wins}</span> | <span class="stat-loss">L: ${stats.losses}</span>`;
    }
}

// Undo last move (undoes both pawn and bobail if mid-turn)
function undoMove() {
    if (gameState.moveHistory.length === 0) return;

    // If we're in pawn phase, we need to undo just the bobail move
    // If we're in bobail phase, we need to undo both moves from the previous turn

    if (gameState.phase === 'pawn') {
        // Undo bobail move only
        const lastMove = gameState.moveHistory.pop();
        gameState.bobailSquare = lastMove.from;
        gameState.phase = 'bobail';
    } else {
        // Undo both pawn and bobail from previous turn
        if (gameState.moveHistory.length >= 2) {
            // Undo pawn
            const pawnMove = gameState.moveHistory.pop();
            const pawns = pawnMove.greenToMove ? gameState.greenPawns : gameState.redPawns;
            const idx = pawns.indexOf(pawnMove.to);
            pawns[idx] = pawnMove.from;

            // Undo bobail
            const bobailMove = gameState.moveHistory.pop();
            gameState.bobailSquare = bobailMove.from;

            gameState.greenToMove = pawnMove.greenToMove;
        } else if (gameState.moveHistory.length === 1) {
            // Only one move (bobail), undo it
            const lastMove = gameState.moveHistory.pop();
            gameState.bobailSquare = lastMove.from;
        }
    }

    gameState.selectedSquare = null;
    gameState.validMoves = [];
    gameState.gameOver = false;
    gameState.winner = null;

    renderBoard();
    updateUI();
}

// Check if AI should move
function shouldAIMove() {
    if (gameState.replayMode) return false; // Don't move during replay
    if (gameState.playerColor === 'both') return false;
    if (gameState.playerColor === 'green' && !gameState.greenToMove) return true;
    if (gameState.playerColor === 'red' && gameState.greenToMove) return true;
    return false;
}

// ==================== Replay Navigation ====================

// Get position at a specific move index (reconstructs from starting position)
function getPositionAtMove(moveIndex) {
    // Start from initial position
    const pos = {
        greenPawns: [0, 1, 2, 3, 4],
        redPawns: [20, 21, 22, 23, 24],
        bobailSquare: 12,
        greenToMove: true,
        phase: 'pawn' // First move is pawn only
    };

    // Apply moves up to the given index
    for (let i = 0; i < moveIndex && i < gameState.moveHistory.length; i++) {
        const move = gameState.moveHistory[i];

        if (move.type === 'bobail') {
            pos.bobailSquare = move.to;
            pos.phase = 'pawn';
        } else if (move.type === 'pawn') {
            const pawns = move.greenToMove ? pos.greenPawns : pos.redPawns;
            const idx = pawns.indexOf(move.from);
            if (idx >= 0) {
                pawns[idx] = move.to;
            }
            pos.greenToMove = !move.greenToMove;
            pos.phase = 'bobail';
        }
    }

    // Handle phase for display
    if (moveIndex > 0) {
        const lastMove = gameState.moveHistory[moveIndex - 1];
        if (lastMove.type === 'bobail') {
            pos.phase = 'pawn';
            pos.greenToMove = lastMove.greenToMove;
        } else {
            pos.phase = 'bobail';
            pos.greenToMove = !lastMove.greenToMove;
        }
    }

    return pos;
}

// Navigate to a specific position in history
function navigateToMove(index) {
    const maxIndex = gameState.moveHistory.length;
    index = Math.max(0, Math.min(index, maxIndex));

    if (index === maxIndex) {
        // Return to current game state (exit replay mode)
        gameState.replayMode = false;
        gameState.replayIndex = maxIndex;
    } else {
        // Enter/continue replay mode
        gameState.replayMode = true;
        gameState.replayIndex = index;
    }

    gameState.selectedSquare = null;
    gameState.validMoves = [];

    renderBoard();
    updateUI();
    updateNavButtons();
}

// Update navigation button states
function updateNavButtons() {
    const maxIndex = gameState.moveHistory.length;
    const current = gameState.replayMode ? gameState.replayIndex : maxIndex;

    // Update both nav bars (history panel and board nav)
    const navIds = [
        ['nav-start', 'nav-prev', 'nav-next', 'nav-end', 'nav-position'],
        ['board-nav-start', 'board-nav-prev', 'board-nav-next', 'board-nav-end', 'board-nav-position']
    ];

    navIds.forEach(([startId, prevId, nextId, endId, posId]) => {
        const startBtn = document.getElementById(startId);
        const prevBtn = document.getElementById(prevId);
        const nextBtn = document.getElementById(nextId);
        const endBtn = document.getElementById(endId);
        const posEl = document.getElementById(posId);

        if (startBtn) startBtn.disabled = current === 0;
        if (prevBtn) prevBtn.disabled = current === 0;
        if (nextBtn) nextBtn.disabled = current >= maxIndex;
        if (endBtn) endBtn.disabled = current >= maxIndex;
        if (posEl) posEl.textContent = `${current}/${maxIndex}`;
    });
}

// ==================== AI with Alpha-Beta Search ====================

// Evaluation function - returns score from Green's perspective
function evaluate(state) {
    const bobailRow = Math.floor(state.bobailSquare / BOARD_SIZE);

    // Terminal conditions
    if (bobailRow === 0) return 10000; // Green wins
    if (bobailRow === BOARD_SIZE - 1) return -10000; // Red wins

    // Check if bobail is trapped
    const bobailMoves = getBobailMovesForState(state);
    if (bobailMoves.length === 0) {
        // The opponent trapped the bobail, so opponent wins
        // If it's Green's turn and bobail is trapped, Red wins (trapped Green)
        return state.greenToMove ? -10000 : 10000;
    }

    let score = 0;

    // Bobail position - closer to home row is better for that player
    // Green wants bobail near row 0, Red wants near row 4
    // From Green's perspective: bobail near row 0 is good (positive)
    score += (BOARD_SIZE - 1 - bobailRow) * 100; // Higher = bobail closer to Green's home (row 0)

    // Bobail mobility
    score += bobailMoves.length * 10;

    // Pawn positioning - control center and block opponent
    for (const sq of state.greenPawns) {
        const [r, c] = toRowCol(sq);
        // Pawns closer to center columns are better
        score += (2 - Math.abs(c - 2)) * 5;
        // Pawns further from home row can push bobail
        score += r * 3;
    }

    for (const sq of state.redPawns) {
        const [r, c] = toRowCol(sq);
        score -= (2 - Math.abs(c - 2)) * 5;
        score -= (BOARD_SIZE - 1 - r) * 3;
    }

    return score;
}

// Get bobail moves for a given state (not using global gameState)
function getBobailMovesForState(state) {
    const moves = [];
    const [row, col] = toRowCol(state.bobailSquare);
    const occupied = new Set([...state.greenPawns, ...state.redPawns, state.bobailSquare]);

    for (const [dr, dc] of DIRECTIONS) {
        const newRow = row + dr;
        const newCol = col + dc;
        if (isValidSquare(newRow, newCol)) {
            const newSq = toSquare(newRow, newCol);
            if (!occupied.has(newSq)) {
                moves.push(newSq);
            }
        }
    }
    return moves;
}

// Get pawn moves for a given state
function getPawnMovesForState(state, sq) {
    const moves = [];
    const [row, col] = toRowCol(sq);
    const occupied = new Set([...state.greenPawns, ...state.redPawns, state.bobailSquare]);

    for (const [dr, dc] of DIRECTIONS) {
        let newRow = row + dr;
        let newCol = col + dc;
        let lastValidSq = null;

        while (isValidSquare(newRow, newCol)) {
            const newSq = toSquare(newRow, newCol);
            if (occupied.has(newSq)) break;

            // Official rules: must move as far as possible
            lastValidSq = newSq;
            newRow += dr;
            newCol += dc;
        }

        if (lastValidSq !== null) {
            moves.push(lastValidSq);
        }
    }
    return moves;
}

// Generate all full moves (bobail + pawn) for a state
function generateFullMoves(state) {
    const moves = [];
    const bobailMoves = getBobailMovesForState(state);

    for (const bobailTo of bobailMoves) {
        // Create state after bobail move
        const afterBobail = {
            ...state,
            bobailSquare: bobailTo
        };

        const pawns = state.greenToMove ? state.greenPawns : state.redPawns;

        for (let i = 0; i < pawns.length; i++) {
            const pawnSq = pawns[i];
            const pawnMoves = getPawnMovesForState(afterBobail, pawnSq);

            for (const pawnTo of pawnMoves) {
                moves.push({
                    bobailFrom: state.bobailSquare,
                    bobailTo: bobailTo,
                    pawnFrom: pawnSq,
                    pawnTo: pawnTo,
                    pawnIndex: i
                });
            }
        }
    }

    return moves;
}

// Apply a full move to a state and return new state
function applyFullMove(state, move) {
    const newGreenPawns = [...state.greenPawns];
    const newRedPawns = [...state.redPawns];

    if (state.greenToMove) {
        newGreenPawns[move.pawnIndex] = move.pawnTo;
    } else {
        newRedPawns[move.pawnIndex] = move.pawnTo;
    }

    return {
        greenPawns: newGreenPawns,
        redPawns: newRedPawns,
        bobailSquare: move.bobailTo,
        greenToMove: !state.greenToMove
    };
}

// Check if state is terminal
function isTerminal(state) {
    const bobailRow = Math.floor(state.bobailSquare / BOARD_SIZE);
    if (bobailRow === 0 || bobailRow === BOARD_SIZE - 1) return true;
    if (getBobailMovesForState(state).length === 0) return true;
    return false;
}

// Alpha-beta search
function alphaBeta(state, depth, alpha, beta, maximizing) {
    if (depth === 0 || isTerminal(state)) {
        return { score: evaluate(state), move: null };
    }

    const moves = generateFullMoves(state);
    if (moves.length === 0) {
        return { score: evaluate(state), move: null };
    }

    let bestMove = moves[0];

    if (maximizing) {
        let maxScore = -Infinity;
        for (const move of moves) {
            const newState = applyFullMove(state, move);
            const result = alphaBeta(newState, depth - 1, alpha, beta, false);
            if (result.score > maxScore) {
                maxScore = result.score;
                bestMove = move;
            }
            alpha = Math.max(alpha, result.score);
            if (beta <= alpha) break;
        }
        return { score: maxScore, move: bestMove };
    } else {
        let minScore = Infinity;
        for (const move of moves) {
            const newState = applyFullMove(state, move);
            const result = alphaBeta(newState, depth - 1, alpha, beta, true);
            if (result.score < minScore) {
                minScore = result.score;
                bestMove = move;
            }
            beta = Math.min(beta, result.score);
            if (beta <= alpha) break;
        }
        return { score: minScore, move: bestMove };
    }
}

// Show/hide thinking indicator
function setThinking(thinking) {
    gameState.aiThinking = thinking;
    const turnIndicator = document.getElementById('turn-indicator');
    if (turnIndicator) {
        turnIndicator.classList.toggle('thinking', thinking);
    }
    updateUI();
}

// AI makes a move using alpha-beta search
function makeAIMove() {
    if (gameState.gameOver || gameState.animating) return;
    if (!shouldAIMove()) return; // Safety check - don't move if not AI's turn

    setThinking(true);

    // Use setTimeout to allow UI to update before heavy computation
    setTimeout(async () => {
        await doAIMove();
        // Note: for perfect AI, setThinking(false) is called inside doAIMovePerfect
        // For regular AI, we call it here
        if (DIFFICULTY[gameState.difficulty] !== 'perfect') {
            setThinking(false);
        }
    }, 50);
}

// Actual AI move logic
async function doAIMove() {
    // Handle first move (pawn only, no bobail - core Bobail rule)
    if (gameState.isFirstMove) {
        // On first move, just pick a pawn move (no bobail move)
        const pawns = gameState.greenToMove ? gameState.greenPawns : gameState.redPawns;
        // Simple heuristic: move a central pawn toward center
        const centerPawn = pawns[2]; // Middle pawn
        const pawnMoves = getPawnMovesForState({
            greenPawns: [...gameState.greenPawns],
            redPawns: [...gameState.redPawns],
            bobailSquare: gameState.bobailSquare
        }, centerPawn);

        if (pawnMoves.length > 0) {
            gameState.selectedSquare = centerPawn;
            // Pick a move toward center (square 12)
            const bestMove = pawnMoves.reduce((best, sq) => {
                const distToBobail = Math.abs(sq - gameState.bobailSquare);
                const bestDist = Math.abs(best - gameState.bobailSquare);
                return distToBobail < bestDist ? sq : best;
            }, pawnMoves[0]);
            makeMove(bestMove);
        }
        return;
    }

    // Check if using perfect solver (impossible difficulty)
    if (DIFFICULTY[gameState.difficulty] === 'perfect') {
        await doAIMovePerfect();
        return;
    }

    // Convert gameState to search state
    const state = {
        greenPawns: [...gameState.greenPawns],
        redPawns: [...gameState.redPawns],
        bobailSquare: gameState.bobailSquare,
        greenToMove: gameState.greenToMove
    };

    // Search depth based on difficulty
    const depth = DIFFICULTY[gameState.difficulty] || 3;
    const maximizing = state.greenToMove;

    const result = alphaBeta(state, depth, -Infinity, Infinity, maximizing);

    if (result.move) {
        // Make bobail move
        if (gameState.phase === 'bobail') {
            makeMove(result.move.bobailTo);
        }

        // Make pawn move (after animation completes)
        setTimeout(() => {
            if (gameState.phase === 'pawn' && !gameState.gameOver && !gameState.animating && shouldAIMove()) {
                gameState.selectedSquare = result.move.pawnFrom;
                makeMove(result.move.pawnTo);
            }
        }, 350);
    }
}

// AI move using perfect solver database
async function doAIMovePerfect() {
    const state = {
        greenPawns: [...gameState.greenPawns],
        redPawns: [...gameState.redPawns],
        bobailSquare: gameState.bobailSquare,
        greenToMove: gameState.greenToMove
    };

    // Build list of all possible moves first, then evaluate in parallel
    const bobailMoves = getBobailMoves();
    const moveList = [];

    for (const bobTo of bobailMoves) {
        const midState = {
            greenPawns: [...state.greenPawns],
            redPawns: [...state.redPawns],
            bobailSquare: bobTo,
            greenToMove: state.greenToMove
        };

        const pawns = state.greenToMove ? state.greenPawns : state.redPawns;
        for (const pawnFrom of pawns) {
            const pawnMoves = getPawnMovesFrom(pawnFrom, midState);
            for (const pawnTo of pawnMoves) {
                const pawnList = state.greenToMove ? [...state.greenPawns] : [...state.redPawns];
                const idx = pawnList.indexOf(pawnFrom);
                if (idx >= 0) pawnList[idx] = pawnTo;

                const afterState = {
                    greenPawns: state.greenToMove ? pawnList : [...state.greenPawns],
                    redPawns: state.greenToMove ? [...state.redPawns] : pawnList,
                    bobailSquare: bobTo,
                    greenToMove: !state.greenToMove
                };

                moveList.push({ bobailTo: bobTo, pawnFrom, pawnTo, afterState });
            }
        }
    }

    // Evaluate moves with a timeout to prevent hanging
    console.log(`Perfect AI evaluating ${moveList.length} moves...`);

    const evalWithTimeout = async (move) => {
        try {
            const result = await lookupPosition(move.afterState);
            if (!result || result.result === 'unknown') {
                return { ...move, eval: 'unknown' };
            }
            let evalForUs;
            if (result.result === 'win') evalForUs = 'loss';
            else if (result.result === 'loss') evalForUs = 'win';
            else evalForUs = 'draw';
            return { ...move, eval: evalForUs };
        } catch (e) {
            return { ...move, eval: 'unknown' };
        }
    };

    // Add overall timeout - if evaluation takes too long, just pick randomly
    let allMoves;
    try {
        const timeoutPromise = new Promise((_, reject) =>
            setTimeout(() => reject(new Error('timeout')), 5000));
        allMoves = await Promise.race([
            Promise.all(moveList.map(evalWithTimeout)),
            timeoutPromise
        ]);
    } catch (e) {
        console.log('Perfect AI evaluation timed out, picking random move');
        allMoves = moveList.map(m => ({ ...m, eval: 'unknown' }));
    }

    // Count evaluations for debugging
    const counts = { win: 0, draw: 0, loss: 0, unknown: 0 };
    for (const m of allMoves) counts[m.eval]++;
    console.log('Perfect AI move evaluations:', counts);

    // Find the best move: prefer win > draw > loss > unknown
    let bestMove = null;
    const evalOrder = { win: 0, draw: 1, unknown: 2, loss: 3 };

    for (const move of allMoves) {
        if (!bestMove || evalOrder[move.eval] < evalOrder[bestMove.eval]) {
            bestMove = move;
        }
    }

    console.log('Perfect AI best move:', bestMove ?
        `B->${bestMove.bobailTo} P:${bestMove.pawnFrom}->${bestMove.pawnTo} (${bestMove.eval})` : 'none');

    if (bestMove) {
        // Make bobail move
        console.log('Making bobail move to:', bestMove.bobailTo);
        if (gameState.phase === 'bobail') {
            makeMove(bestMove.bobailTo);
        }

        // Make pawn move (after animation completes)
        setTimeout(() => {
            console.log('Pawn move timeout fired. State:', {
                phase: gameState.phase,
                gameOver: gameState.gameOver,
                animating: gameState.animating,
                shouldAI: shouldAIMove(),
                pawnFrom: bestMove.pawnFrom,
                pawnTo: bestMove.pawnTo
            });
            if (gameState.phase === 'pawn' && !gameState.gameOver && !gameState.animating && shouldAIMove()) {
                gameState.selectedSquare = bestMove.pawnFrom;
                console.log('Making pawn move from', bestMove.pawnFrom, 'to', bestMove.pawnTo);
                makeMove(bestMove.pawnTo);
            } else {
                console.log('Pawn move conditions not met!');
            }
        }, 350);
    } else {
        console.log('No best move found!');
    }

    setThinking(false);
}

// Handle square click in setup mode
function handleSetupClick(sq) {
    const currentPiece = getPieceAt(sq);

    // Remove any existing piece at this square
    if (currentPiece === 'bobail') {
        gameState.bobailSquare = -1; // No bobail
    } else if (currentPiece === 'green') {
        gameState.greenPawns = gameState.greenPawns.filter(s => s !== sq);
    } else if (currentPiece === 'red') {
        gameState.redPawns = gameState.redPawns.filter(s => s !== sq);
    }

    // Place new piece based on selected type
    if (gameState.setupPiece === 'clear') {
        // Already cleared above
    } else if (gameState.setupPiece === 'bobail') {
        gameState.bobailSquare = sq;
    } else if (gameState.setupPiece === 'green') {
        if (!gameState.greenPawns.includes(sq)) {
            gameState.greenPawns.push(sq);
        }
    } else if (gameState.setupPiece === 'red') {
        if (!gameState.redPawns.includes(sq)) {
            gameState.redPawns.push(sq);
        }
    }

    renderBoard();
    updateUI();
}

// Handle square click
function handleSquareClick(sq) {
    // Setup mode has its own handler
    if (gameState.setupMode) {
        handleSetupClick(sq);
        return;
    }

    if (gameState.gameOver) return;
    if (gameState.animating) return;
    if (shouldAIMove()) return; // Not player's turn

    const piece = getPieceAt(sq);

    if (gameState.phase === 'bobail') {
        // Bobail phase - can only move bobail
        if (piece === 'bobail') {
            // Select bobail
            gameState.selectedSquare = sq;
            gameState.validMoves = getBobailMoves();
            renderBoard();
            if (gameState.hintsEnabled) requestHints();
        } else if (gameState.validMoves.includes(sq)) {
            // Move bobail to this square
            makeMove(sq);
        }
    } else {
        // Pawn phase
        const myPawns = gameState.greenToMove ? gameState.greenPawns : gameState.redPawns;

        if (myPawns.includes(sq)) {
            // Select this pawn
            gameState.selectedSquare = sq;
            gameState.validMoves = getPawnMoves(sq);
            renderBoard();
            if (gameState.hintsEnabled) requestHints();
        } else if (gameState.validMoves.includes(sq)) {
            // Move selected pawn to this square
            makeMove(sq);
        }
    }
}

// Render the board
function renderBoard() {
    const board = document.getElementById('board');
    board.innerHTML = '';

    // Get position to display (replay or current)
    let displayPos;
    if (gameState.replayMode) {
        displayPos = getPositionAtMove(gameState.replayIndex);
    } else {
        displayPos = {
            greenPawns: gameState.greenPawns,
            redPawns: gameState.redPawns,
            bobailSquare: gameState.bobailSquare,
            greenToMove: gameState.greenToMove,
            phase: gameState.phase
        };
    }

    // Helper to get piece at square for display position
    const getPieceAtDisplay = (sq) => {
        if (sq === displayPos.bobailSquare) return 'bobail';
        if (displayPos.greenPawns.includes(sq)) return 'green';
        if (displayPos.redPawns.includes(sq)) return 'red';
        return null;
    };

    for (let row = 0; row < BOARD_SIZE; row++) {
        for (let col = 0; col < BOARD_SIZE; col++) {
            const sq = toSquare(row, col);
            const square = document.createElement('div');
            square.className = 'square';
            square.classList.add((row + col) % 2 === 0 ? 'light' : 'dark');
            square.dataset.square = sq;

            // Highlight selected square (only when not in replay mode)
            if (!gameState.replayMode && sq === gameState.selectedSquare) {
                square.classList.add('selected');
            }

            // Show valid moves (only when not in replay mode)
            if (!gameState.replayMode && gameState.validMoves.includes(sq)) {
                square.classList.add('valid-move');
                if (isOccupied(sq)) {
                    square.classList.add('has-piece');
                }
            }

            // Add hints if enabled (only when not in replay mode)
            if (!gameState.replayMode && gameState.hintsEnabled && !gameState.gameOver) {
                const hint = getMoveHint(sq);
                if (hint) {
                    square.classList.add(`hint-${hint}`);
                }
            }

            // Add piece if present
            const piece = getPieceAtDisplay(sq);
            if (piece) {
                const pieceEl = document.createElement('div');
                pieceEl.className = `piece ${piece}`;

                // Add pulsing effect to bobail when player needs to move it (not during replay)
                if (!gameState.replayMode && piece === 'bobail' && gameState.phase === 'bobail' && !gameState.gameOver && !shouldAIMove()) {
                    pieceEl.classList.add('needs-move');
                }

                square.appendChild(pieceEl);
            }

            square.addEventListener('click', () => handleSquareClick(sq));
            board.appendChild(square);
        }
    }
}

// ==================== Perfect Solver Integration ====================

// Server endpoint for perfect solver (localhost when running locally)
const SOLVER_SERVER = 'http://localhost:8081';

// Cache for solver lookups to avoid repeated requests
const solverCache = new Map();

// Convert current game state to position string for solver query
function stateToSolverPos(state) {
    // Format: WP,BP,BOB,STM (hex,hex,int,int)
    // WP and BP are bitmasks where bit i = pawn on square i
    let wp = 0, bp = 0;
    for (const sq of state.greenPawns) wp |= (1 << sq);
    for (const sq of state.redPawns) bp |= (1 << sq);
    const stm = state.greenToMove ? 1 : 0;
    return `${wp.toString(16)},${bp.toString(16)},${state.bobailSquare},${stm}`;
}

// Lookup position in the perfect solver (async)
async function lookupPosition(state) {
    const pos = stateToSolverPos(state);

    // Check cache first
    if (solverCache.has(pos)) {
        return solverCache.get(pos);
    }

    try {
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 3000); // 3s timeout

        const response = await fetch(`${SOLVER_SERVER}/lookup?pos=${pos}`, {
            signal: controller.signal
        });
        clearTimeout(timeout);

        if (!response.ok) return null;

        const data = await response.json();
        solverCache.set(pos, data);
        return data;
    } catch (e) {
        // Server not available or timeout - return null
        console.log('Solver lookup failed:', e.message);
        return null;
    }
}

// Get evaluation for a specific move (what result would this lead to?)
// Returns 'win', 'draw', 'loss', or null if unknown
async function getMoveEvaluation(fromState, move, phase) {
    if (phase === 'bobail') {
        // For bobail moves: evaluate based on best possible pawn follow-up
        // - If ANY pawn follow-up wins -> bobail move is WINNING
        // - Else if ANY pawn follow-up draws -> bobail move is DRAW
        // - Else all pawn follow-ups lose -> bobail move is LOSING

        const midState = {
            greenPawns: [...fromState.greenPawns],
            redPawns: [...fromState.redPawns],
            bobailSquare: move,
            greenToMove: fromState.greenToMove
        };

        // Get all possible pawn moves after this bobail move
        const pawns = fromState.greenToMove ? fromState.greenPawns : fromState.redPawns;
        const pawnMoves = [];

        for (const pawnSq of pawns) {
            const moves = getPawnMovesFrom(pawnSq, midState);
            for (const toSq of moves) {
                pawnMoves.push({ from: pawnSq, to: toSq });
            }
        }

        if (pawnMoves.length === 0) return 'loss'; // No pawn moves = stuck = lose

        // Evaluate each pawn follow-up
        let bestEval = 'loss';
        for (const pm of pawnMoves) {
            const pawnList = fromState.greenToMove ? [...fromState.greenPawns] : [...fromState.redPawns];
            const idx = pawnList.indexOf(pm.from);
            if (idx >= 0) pawnList[idx] = pm.to;

            const afterState = {
                greenPawns: fromState.greenToMove ? pawnList : [...fromState.greenPawns],
                redPawns: fromState.greenToMove ? [...fromState.redPawns] : pawnList,
                bobailSquare: move,
                greenToMove: !fromState.greenToMove  // Turn ends after pawn move
            };

            const result = await lookupPosition(afterState);
            if (!result || result.result === 'unknown') continue;

            // Flip perspective: result is from opponent's view
            let evalForUs;
            if (result.result === 'win') evalForUs = 'loss';
            else if (result.result === 'loss') evalForUs = 'win';
            else evalForUs = 'draw';

            // Keep the best evaluation
            if (evalForUs === 'win') {
                bestEval = 'win';
                break;  // Can't do better than win
            } else if (evalForUs === 'draw' && bestEval === 'loss') {
                bestEval = 'draw';
            }
        }

        return bestEval;
    } else {
        // For pawn moves: evaluate the resulting position directly
        const pawns = fromState.greenToMove ? [...fromState.greenPawns] : [...fromState.redPawns];
        const idx = pawns.indexOf(gameState.selectedSquare);
        if (idx >= 0) pawns[idx] = move;

        const afterState = {
            greenPawns: fromState.greenToMove ? pawns : [...fromState.greenPawns],
            redPawns: fromState.greenToMove ? [...fromState.redPawns] : pawns,
            bobailSquare: fromState.bobailSquare,
            greenToMove: !fromState.greenToMove  // Turn ends after pawn move
        };

        const result = await lookupPosition(afterState);
        if (!result || result.result === 'unknown') return null;

        // Result is from the perspective of the side to move AFTER the move (opponent)
        // So we need to flip it to get our perspective
        if (result.result === 'win') return 'loss';  // Opponent wins = we lose
        if (result.result === 'loss') return 'win';  // Opponent loses = we win
        return 'draw';
    }
}

// Note: pendingHints, hintRequestPending, currentPositionEval are declared at the top of the file

// Request hints for all valid moves (called when hints are enabled)
async function requestHints() {
    if (!gameState.hintsEnabled || gameState.gameOver || hintRequestPending) return;
    if (gameState.validMoves.length === 0) return;

    hintRequestPending = true;
    pendingHints.clear();

    // Show loading indicator
    showHintLoading(true);

    const currentState = {
        greenPawns: [...gameState.greenPawns],
        redPawns: [...gameState.redPawns],
        bobailSquare: gameState.bobailSquare,
        greenToMove: gameState.greenToMove
    };

    // Request evaluations for all valid moves in parallel
    const promises = gameState.validMoves.map(async (sq) => {
        const evaluation = await getMoveEvaluation(currentState, sq, gameState.phase);
        pendingHints.set(sq, evaluation);
    });

    await Promise.all(promises);
    hintRequestPending = false;

    // Hide loading indicator
    showHintLoading(false);

    // Update best moves display
    updateBestMovesDisplay();

    // Re-render to show hints
    renderBoard();
}

// Show/hide hint loading indicator
function showHintLoading(loading) {
    const hintLegend = document.getElementById('hint-legend');
    if (!hintLegend) return;

    if (loading) {
        hintLegend.innerHTML = `
            <h4>Move Evaluation</h4>
            <div class="hint-loading">
                <span class="loading-spinner"></span>
                Analyzing moves...
            </div>
        `;
    }
}

// Update the position evaluation display
function updatePositionEvalDisplay() {
    const evalValue = document.getElementById('eval-value');
    if (evalValue && currentPositionEval) {
        evalValue.textContent = currentPositionEval;
        evalValue.className = 'eval-value';
        if (currentPositionEval === 'WIN') evalValue.classList.add('win');
        else if (currentPositionEval === 'LOSS') evalValue.classList.add('loss');
        else if (currentPositionEval === 'DRAW') evalValue.classList.add('draw');
    }
}

// Reset the hint legend to default state
function resetHintLegend() {
    const hintLegend = document.getElementById('hint-legend');
    if (hintLegend) {
        hintLegend.innerHTML = `
            <h4>Move Evaluation</h4>
            <div class="legend-item">
                <span class="legend-color win"></span> Winning move
            </div>
            <div class="legend-item">
                <span class="legend-color draw"></span> Drawing move
            </div>
            <div class="legend-item">
                <span class="legend-color loss"></span> Losing move
            </div>
        `;
    }
    // Also reset position eval display
    const evalValue = document.getElementById('eval-value');
    if (evalValue) {
        evalValue.textContent = '--';
        evalValue.className = 'eval-value';
    }
}

// Update the best moves display (show winning moves)
function updateBestMovesDisplay() {
    // Count moves by evaluation
    const winMoves = [];
    const drawMoves = [];
    const lossMoves = [];

    for (const [sq, evaluation] of pendingHints) {
        if (evaluation === 'win') winMoves.push(sq);
        else if (evaluation === 'draw') drawMoves.push(sq);
        else if (evaluation === 'loss') lossMoves.push(sq);
    }

    // Update hint legend with counts - show which player's perspective
    const hintLegend = document.getElementById('hint-legend');
    const currentPlayer = gameState.greenToMove ? 'Green' : 'Red';
    const playerColor = gameState.greenToMove ? 'var(--green-piece)' : 'var(--red-piece)';

    if (hintLegend) {
        hintLegend.innerHTML = `
            <h4>Moves for <span style="color: ${playerColor}">${currentPlayer}</span></h4>
            <div class="legend-item">
                <span class="legend-color win"></span> Winning: ${winMoves.length} move${winMoves.length !== 1 ? 's' : ''}
            </div>
            <div class="legend-item">
                <span class="legend-color draw"></span> Drawing: ${drawMoves.length} move${drawMoves.length !== 1 ? 's' : ''}
            </div>
            <div class="legend-item">
                <span class="legend-color loss"></span> Losing: ${lossMoves.length} move${lossMoves.length !== 1 ? 's' : ''}
            </div>
        `;

        // If there are winning moves, show top 5
        if (winMoves.length > 0) {
            const moveList = winMoves.slice(0, 5).map(sq => squareToNotation(sq)).join(', ');
            const moreText = winMoves.length > 5 ? ` (+${winMoves.length - 5} more)` : '';
            hintLegend.innerHTML += `
                <div class="best-moves">
                    <strong>Best moves:</strong> ${moveList}${moreText}
                </div>
            `;
        } else if (drawMoves.length > 0 && lossMoves.length > 0) {
            // No winning moves, show drawing moves
            const moveList = drawMoves.slice(0, 5).map(sq => squareToNotation(sq)).join(', ');
            const moreText = drawMoves.length > 5 ? ` (+${drawMoves.length - 5} more)` : '';
            hintLegend.innerHTML += `
                <div class="best-moves">
                    <strong>Best (draw):</strong> ${moveList}${moreText}
                </div>
            `;
        }
    }
}

// Get hint for a square (uses cached solver results)
function getMoveHint(sq) {
    if (!gameState.validMoves.includes(sq)) return null;

    // Return cached hint if available
    if (pendingHints.has(sq)) {
        return pendingHints.get(sq);
    }

    // No hint available yet
    return null;
}

// Update UI elements
function updateUI() {
    // Turn indicator
    const turnText = document.getElementById('turn-text');
    const turnDot = document.querySelector('.turn-dot');

    if (gameState.replayMode) {
        const pos = getPositionAtMove(gameState.replayIndex);
        turnText.textContent = `Reviewing: Move ${gameState.replayIndex}`;
        turnDot.className = 'turn-dot';
        turnDot.classList.add(pos.greenToMove ? 'green-dot' : 'red-dot');
    } else if (gameState.setupMode) {
        turnText.textContent = 'Setup Mode - Click to place pieces';
        turnDot.className = 'turn-dot';
    } else if (gameState.aiThinking) {
        turnText.textContent = `${gameState.greenToMove ? 'Green' : 'Red'} is thinking...`;
    } else if (gameState.gameOver) {
        turnText.textContent = `${gameState.winner === 'green' ? 'Green' : 'Red'} wins!`;
    } else {
        const phaseText = gameState.phase === 'bobail' ? 'Move Bobail' : 'Move Pawn';
        turnText.textContent = `${gameState.greenToMove ? 'Green' : 'Red'}: ${phaseText}`;
    }

    if (!gameState.setupMode && !gameState.replayMode) {
        turnDot.className = 'turn-dot';
        turnDot.classList.add(gameState.greenToMove ? 'green-dot' : 'red-dot');
    }

    // Evaluation (placeholder)
    const evalValue = document.getElementById('eval-value');
    evalValue.textContent = '--';
    evalValue.className = 'eval-value';

    // Hint legend visibility
    const hintLegend = document.getElementById('hint-legend');
    hintLegend.classList.toggle('visible', gameState.hintsEnabled);

    // Move history
    updateMoveHistory();

    // Navigation buttons
    updateNavButtons();

    // Show game over overlay if needed
    if (gameState.gameOver && !gameState.replayMode) {
        showGameOverOverlay();
    }
}

// Update move history display
function updateMoveHistory() {
    const moveList = document.getElementById('move-list');
    moveList.innerHTML = '';

    let moveNum = 1;
    let i = 0;

    while (i < gameState.moveHistory.length) {
        const entry = document.createElement('div');
        entry.className = 'move-entry';

        const firstMove = gameState.moveHistory[i];
        const color = firstMove.greenToMove ? 'G' : 'R';
        let text = `<span class="move-number">${moveNum}.</span>${color}: `;

        // Check if first move is pawn-only (first turn of game) or bobail+pawn
        if (firstMove.type === 'pawn') {
            // Pawn-only move (first turn)
            text += `${squareToNotation(firstMove.from)}-${squareToNotation(firstMove.to)}`;
            i++;
        } else {
            // Bobail move
            text += `B${squareToNotation(firstMove.from)}-${squareToNotation(firstMove.to)}`;
            i++;

            // Check for following pawn move
            if (i < gameState.moveHistory.length && gameState.moveHistory[i].type === 'pawn') {
                const pawnMove = gameState.moveHistory[i];
                text += `, ${squareToNotation(pawnMove.from)}-${squareToNotation(pawnMove.to)}`;
                i++;
            }
        }

        entry.innerHTML = text;
        moveList.appendChild(entry);
        moveNum++;
    }

    // Scroll to bottom
    moveList.scrollTop = moveList.scrollHeight;
}

// Convert square to algebraic notation (a1-e5)
function squareToNotation(sq) {
    const [row, col] = toRowCol(sq);
    const file = String.fromCharCode(97 + col); // a-e
    const rank = BOARD_SIZE - row; // 5-1 (top to bottom)
    return `${file}${rank}`;
}

// Show game over overlay
function showGameOverOverlay() {
    // Create overlay if it doesn't exist
    let overlay = document.querySelector('.game-over-overlay');
    if (!overlay) {
        overlay = document.createElement('div');
        overlay.className = 'game-over-overlay';
        overlay.innerHTML = `
            <div class="game-over-content">
                <h2 id="game-over-title"></h2>
                <p id="game-over-message"></p>
                <div class="game-over-buttons">
                    <button class="btn" onclick="hideGameOverOverlay(); initGame();">Play Again</button>
                    <button class="btn btn-secondary" onclick="hideGameOverOverlay();">Review Game</button>
                </div>
            </div>
        `;
        document.body.appendChild(overlay);
    }

    const title = document.getElementById('game-over-title');
    const message = document.getElementById('game-over-message');

    const isPlayerWin = gameState.playerColor !== 'both' && gameState.playerColor === gameState.winner;
    const isPlayerLoss = gameState.playerColor !== 'both' && gameState.playerColor !== gameState.winner;

    if (isPlayerWin) {
        title.textContent = 'You Win!';
        title.style.color = 'var(--win-color)';
    } else if (isPlayerLoss) {
        title.textContent = 'You Lose!';
        title.style.color = 'var(--loss-color)';
    } else {
        title.textContent = `${gameState.winner === 'green' ? 'Green' : 'Red'} Wins!`;
        title.style.color = '';
    }

    const bobailRow = Math.floor(gameState.bobailSquare / BOARD_SIZE);
    if (bobailRow === 0 || bobailRow === BOARD_SIZE - 1) {
        message.textContent = 'The Bobail reached the home row.';
    } else {
        message.textContent = 'The Bobail was trapped!';
    }

    setTimeout(() => overlay.classList.add('visible'), 100);
}

function hideGameOverOverlay() {
    const overlay = document.querySelector('.game-over-overlay');
    if (overlay) {
        overlay.classList.remove('visible');
    }
}

// ==================== URL Sharing ====================

// Encode game state to a compact string
function encodeGameState() {
    const wp = gameState.greenPawns.map(s => s.toString(36)).join('');
    const bp = gameState.redPawns.map(s => s.toString(36)).join('');
    const bob = gameState.bobailSquare.toString(36);
    const turn = gameState.greenToMove ? 'w' : 'b';
    const phase = gameState.phase === 'bobail' ? 'B' : 'P';
    return `${wp}-${bp}-${bob}${turn}${phase}`;
}

// Decode game state from URL parameter
function decodeGameState(code) {
    try {
        const parts = code.split('-');
        if (parts.length !== 3) return null;

        const wp = parts[0].split('').map(c => parseInt(c, 36));
        const bp = parts[1].split('').map(c => parseInt(c, 36));

        const bobTurnPhase = parts[2];
        const bob = parseInt(bobTurnPhase[0], 36);
        const turn = bobTurnPhase[1] === 'w';
        const phase = bobTurnPhase[2] === 'B' ? 'bobail' : 'pawn';

        // Validate
        if (wp.length !== 5 || bp.length !== 5) return null;
        if (bob < 0 || bob >= 25) return null;

        return {
            greenPawns: wp,
            redPawns: bp,
            bobailSquare: bob,
            greenToMove: turn,
            phase: phase
        };
    } catch (e) {
        return null;
    }
}

// Exit setup mode and validate position
function exitSetupMode() {
    // Check if position is valid
    const hasValidBobail = gameState.bobailSquare >= 0 && gameState.bobailSquare < 25;

    if (!hasValidBobail) {
        showToast('Please place the Bobail on the board');
        return false;
    }

    if (gameState.greenPawns.length === 0 && gameState.redPawns.length === 0) {
        showToast('Please place at least one pawn');
        return false;
    }

    // Set who moves based on checkbox
    const greenToMoveCheckbox = document.getElementById('setup-green-to-move');
    gameState.greenToMove = greenToMoveCheckbox ? greenToMoveCheckbox.checked : true;

    // Reset game state for play
    gameState.phase = 'bobail';
    gameState.selectedSquare = null;
    gameState.validMoves = [];
    gameState.moveHistory = [];
    gameState.gameOver = false;
    gameState.winner = null;
    gameState.isFirstMove = false; // Not first move in custom setup

    // Check for immediate game over
    const result = checkGameOver();
    if (result.over) {
        gameState.gameOver = true;
        gameState.winner = result.winner;
    }

    // Auto-select bobail if it's player's turn
    if (!gameState.gameOver && gameState.phase === 'bobail' && !shouldAIMove()) {
        gameState.selectedSquare = gameState.bobailSquare;
        gameState.validMoves = getBobailMoves();
    }

    showToast('Position set - game started');

    // Trigger AI if needed
    if (!gameState.gameOver && shouldAIMove()) {
        setTimeout(makeAIMove, 500);
    }

    return true;
}

// Share current position via URL
function sharePosition() {
    const code = encodeGameState();
    const url = `${window.location.origin}${window.location.pathname}?pos=${code}`;

    // Copy to clipboard
    navigator.clipboard.writeText(url).then(() => {
        showToast('Position URL copied to clipboard!');
    }).catch(() => {
        // Fallback - show URL
        prompt('Copy this URL to share the position:', url);
    });
}

// Load position from URL on page load
function loadFromURL() {
    const params = new URLSearchParams(window.location.search);
    const pos = params.get('pos');
    if (pos) {
        const state = decodeGameState(pos);
        if (state) {
            gameState.greenPawns = state.greenPawns;
            gameState.redPawns = state.redPawns;
            gameState.bobailSquare = state.bobailSquare;
            gameState.greenToMove = state.greenToMove;
            gameState.phase = state.phase;
            gameState.playerColor = 'both'; // Analysis mode for shared positions
            // Set difficulty to impossible (perfect solver) for testing solver positions
            gameState.difficulty = 'impossible';
            const diffSelect = document.getElementById('difficulty');
            if (diffSelect) diffSelect.value = 'impossible';
            // Auto-select bobail if it's bobail phase
            if (gameState.phase === 'bobail') {
                gameState.selectedSquare = gameState.bobailSquare;
                gameState.validMoves = getBobailMoves();
            }
            renderBoard();
            updateUI();
            showToast('Position loaded - using perfect solver');
        }
    }
}

// Show toast notification
function showToast(message) {
    let toast = document.querySelector('.toast');
    if (!toast) {
        toast = document.createElement('div');
        toast.className = 'toast';
        document.body.appendChild(toast);
    }
    toast.textContent = message;
    toast.classList.add('visible');
    setTimeout(() => toast.classList.remove('visible'), 2000);
}

// Event listeners
document.addEventListener('DOMContentLoaded', () => {
    // Load saved stats, game history, and initialize sounds
    loadStats();
    loadGameHistory();
    initSounds();

    // Initialize game
    initGame();

    // Update game history display
    updateGameHistoryDisplay();

    // Load position from URL if present
    loadFromURL();

    // Hint toggle
    document.getElementById('hint-toggle').addEventListener('click', (e) => {
        gameState.hintsEnabled = !gameState.hintsEnabled;
        e.target.classList.toggle('active', gameState.hintsEnabled);
        e.target.innerHTML = gameState.hintsEnabled
            ? '<span class="hint-icon">💡</span> Hide Hints'
            : '<span class="hint-icon">💡</span> Show Hints';
        // Show/hide evaluation with hints
        const evalEl = document.getElementById('evaluation');
        evalEl.classList.toggle('hidden', !gameState.hintsEnabled);
        renderBoard();
        updateUI();
        // Request hints from solver if enabled
        if (gameState.hintsEnabled && gameState.validMoves.length > 0) {
            requestHints();
        }
    });

    // New game
    document.getElementById('new-game').addEventListener('click', () => {
        hideGameOverOverlay();
        initGame();
    });

    // Undo
    document.getElementById('undo').addEventListener('click', undoMove);

    // History navigation - helper functions
    const navStart = () => navigateToMove(0);
    const navPrev = () => {
        const current = gameState.replayMode ? gameState.replayIndex : gameState.moveHistory.length;
        navigateToMove(current - 1);
    };
    const navNext = () => {
        const current = gameState.replayMode ? gameState.replayIndex : gameState.moveHistory.length;
        navigateToMove(current + 1);
    };
    const navEnd = () => navigateToMove(gameState.moveHistory.length);

    // History panel navigation
    document.getElementById('nav-start').addEventListener('click', navStart);
    document.getElementById('nav-prev').addEventListener('click', navPrev);
    document.getElementById('nav-next').addEventListener('click', navNext);
    document.getElementById('nav-end').addEventListener('click', navEnd);

    // Board navigation (below board)
    document.getElementById('board-nav-start').addEventListener('click', navStart);
    document.getElementById('board-nav-prev').addEventListener('click', navPrev);
    document.getElementById('board-nav-next').addEventListener('click', navNext);
    document.getElementById('board-nav-end').addEventListener('click', navEnd);

    // Player color select
    document.getElementById('player-color').addEventListener('change', (e) => {
        console.log('Player color changed to:', e.target.value);
        gameState.playerColor = e.target.value;
        // Don't reset game, just update and check if AI should move
        renderBoard();
        updateUI();
        console.log('shouldAIMove:', shouldAIMove(), 'greenToMove:', gameState.greenToMove);
        if (shouldAIMove()) {
            console.log('Triggering AI move in 500ms');
            setTimeout(makeAIMove, 500);
        }
    });

    // Difficulty select
    const difficultySelect = document.getElementById('difficulty');
    if (difficultySelect) {
        difficultySelect.addEventListener('change', (e) => {
            gameState.difficulty = e.target.value;
        });
    }

    // Sound toggle
    const soundToggle = document.getElementById('sound-toggle');
    if (soundToggle) {
        soundToggle.addEventListener('click', (e) => {
            sounds.enabled = !sounds.enabled;
            e.target.classList.toggle('active', sounds.enabled);
            e.target.textContent = sounds.enabled ? '🔊' : '🔇';
        });
    }

    // Share button
    const shareBtn = document.getElementById('share-btn');
    if (shareBtn) {
        shareBtn.addEventListener('click', sharePosition);
    }

    // Reset stats button
    const resetStatsBtn = document.getElementById('reset-stats');
    if (resetStatsBtn) {
        resetStatsBtn.addEventListener('click', () => {
            stats = { wins: 0, losses: 0, draws: 0 };
            saveStats();
            updateStatsDisplay();
            showToast('Stats reset');
        });
    }

    // Setup mode button
    const setupBtn = document.getElementById('setup-btn');
    if (setupBtn) {
        setupBtn.addEventListener('click', () => {
            gameState.setupMode = !gameState.setupMode;
            const setupPanel = document.getElementById('setup-panel');
            setupPanel.classList.toggle('hidden', !gameState.setupMode);
            setupBtn.classList.toggle('active', gameState.setupMode);
            setupBtn.textContent = gameState.setupMode ? 'Playing' : 'Setup';

            if (gameState.setupMode) {
                // Entering setup mode
                gameState.gameOver = false;
                gameState.selectedSquare = null;
                gameState.validMoves = [];
                hideGameOverOverlay();
            } else {
                // Exiting setup mode - validate and start game
                exitSetupMode();
            }
            renderBoard();
            updateUI();
        });
    }

    // Setup piece buttons
    document.querySelectorAll('.setup-piece-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.setup-piece-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            gameState.setupPiece = btn.dataset.piece;
        });
    });

    // Setup done button
    const setupDoneBtn = document.getElementById('setup-done');
    if (setupDoneBtn) {
        setupDoneBtn.addEventListener('click', () => {
            exitSetupMode();
            gameState.setupMode = false;
            document.getElementById('setup-panel').classList.add('hidden');
            document.getElementById('setup-btn').classList.remove('active');
            document.getElementById('setup-btn').textContent = 'Setup';
            renderBoard();
            updateUI();
        });
    }

    // Setup clear all button
    const setupClearBtn = document.getElementById('setup-clear-all');
    if (setupClearBtn) {
        setupClearBtn.addEventListener('click', () => {
            gameState.greenPawns = [];
            gameState.redPawns = [];
            gameState.bobailSquare = -1;
            renderBoard();
            updateUI();
        });
    }

    // Setup import button
    const setupImportBtn = document.getElementById('setup-import');
    if (setupImportBtn) {
        setupImportBtn.addEventListener('click', () => {
            const input = prompt('Paste a shared position URL or code:');
            if (!input) return;

            // Extract position code from URL or use raw input
            let code = input.trim();
            if (code.includes('?pos=')) {
                const match = code.match(/[?&]pos=([^&]+)/);
                if (match) code = match[1];
            }

            const state = decodeGameState(code);
            if (state) {
                gameState.greenPawns = state.greenPawns;
                gameState.redPawns = state.redPawns;
                gameState.bobailSquare = state.bobailSquare;
                // Update the checkbox to match imported state
                const checkbox = document.getElementById('setup-green-to-move');
                if (checkbox) checkbox.checked = state.greenToMove;
                renderBoard();
                updateUI();
                showToast('Position imported');
            } else {
                showToast('Invalid position code');
            }
        });
    }

    // Keyboard controls
    document.addEventListener('keydown', (e) => {
        // Arrow keys for history navigation (always available)
        if (e.key === 'ArrowLeft') {
            e.preventDefault();
            const current = gameState.replayMode ? gameState.replayIndex : gameState.moveHistory.length;
            navigateToMove(current - 1);
            return;
        } else if (e.key === 'ArrowRight') {
            e.preventDefault();
            const current = gameState.replayMode ? gameState.replayIndex : gameState.moveHistory.length;
            navigateToMove(current + 1);
            return;
        } else if (e.key === 'Home') {
            e.preventDefault();
            navigateToMove(0);
            return;
        } else if (e.key === 'End') {
            e.preventDefault();
            navigateToMove(gameState.moveHistory.length);
            return;
        }

        if (gameState.gameOver || gameState.animating || shouldAIMove() || gameState.replayMode) return;

        // Other keys only when playing
        if (e.key === 'Escape') {
            gameState.selectedSquare = null;
            gameState.validMoves = [];
            renderBoard();
        } else if (e.key === 'z' && (e.ctrlKey || e.metaKey)) {
            e.preventDefault();
            undoMove();
        } else if (e.key === 'n' && (e.ctrlKey || e.metaKey)) {
            e.preventDefault();
            hideGameOverOverlay();
            initGame();
        }
    });

    // Touch handling for mobile
    let touchStartX = 0;
    let touchStartY = 0;

    document.getElementById('board').addEventListener('touchstart', (e) => {
        touchStartX = e.touches[0].clientX;
        touchStartY = e.touches[0].clientY;
    }, { passive: true });

    document.getElementById('board').addEventListener('touchend', (e) => {
        const touchEndX = e.changedTouches[0].clientX;
        const touchEndY = e.changedTouches[0].clientY;

        // Only register as click if not a swipe
        const dx = Math.abs(touchEndX - touchStartX);
        const dy = Math.abs(touchEndY - touchStartY);

        if (dx < 10 && dy < 10) {
            // It's a tap, let the click handler deal with it
            return;
        }
    }, { passive: true });
});
