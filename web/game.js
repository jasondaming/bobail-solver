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

// Game state
let gameState = {
    whitePawns: [],      // Array of square indices
    blackPawns: [],      // Array of square indices
    bobailSquare: 12,    // Center square
    whiteToMove: true,
    phase: 'bobail',     // 'bobail' or 'pawn'
    selectedSquare: null,
    validMoves: [],
    moveHistory: [],
    hintsEnabled: false,
    playerColor: 'white', // 'white', 'black', or 'both'
    gameOver: false,
    winner: null
};

// Initialize starting position
function initGame() {
    gameState = {
        whitePawns: [0, 1, 2, 3, 4],      // Top row (White's home is row 0)
        blackPawns: [20, 21, 22, 23, 24], // Bottom row (Black's home is row 4)
        bobailSquare: 12,
        whiteToMove: true,
        phase: 'bobail',
        selectedSquare: null,
        validMoves: [],
        moveHistory: [],
        hintsEnabled: gameState.hintsEnabled,
        playerColor: gameState.playerColor,
        gameOver: false,
        winner: null
    };
    renderBoard();
    updateUI();
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
    if (gameState.whitePawns.includes(sq)) return 'white';
    if (gameState.blackPawns.includes(sq)) return 'black';
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

        while (isValidSquare(newRow, newCol)) {
            const newSq = toSquare(newRow, newCol);
            if (isOccupied(newSq)) break;
            moves.push(newSq);
            newRow += dr;
            newCol += dc;
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
    const pawns = gameState.whiteToMove ? gameState.whitePawns : gameState.blackPawns;
    return pawns.filter(sq => getPawnMoves(sq).length > 0);
}

// Check terminal conditions
function checkGameOver() {
    const bobailRow = Math.floor(gameState.bobailSquare / BOARD_SIZE);

    // Bobail on White's home row (row 0) = White wins
    if (bobailRow === 0) {
        return { over: true, winner: 'white' };
    }

    // Bobail on Black's home row (row 4) = Black wins
    if (bobailRow === BOARD_SIZE - 1) {
        return { over: true, winner: 'black' };
    }

    // Check if Bobail is trapped (can't move on opponent's turn)
    // This happens at the start of a turn (bobail phase)
    if (gameState.phase === 'bobail' && getBobailMoves().length === 0) {
        // Current player wins because opponent trapped the bobail
        return { over: true, winner: gameState.whiteToMove ? 'white' : 'black' };
    }

    return { over: false, winner: null };
}

// Make a move
function makeMove(toSquare) {
    if (gameState.phase === 'bobail') {
        // Move bobail
        const fromSquare = gameState.bobailSquare;
        gameState.bobailSquare = toSquare;
        gameState.moveHistory.push({
            type: 'bobail',
            from: fromSquare,
            to: toSquare,
            whiteToMove: gameState.whiteToMove
        });
        gameState.phase = 'pawn';
        gameState.selectedSquare = null;
        gameState.validMoves = [];
    } else {
        // Move pawn
        const fromSquare = gameState.selectedSquare;
        const pawns = gameState.whiteToMove ? gameState.whitePawns : gameState.blackPawns;
        const idx = pawns.indexOf(fromSquare);
        pawns[idx] = toSquare;

        gameState.moveHistory.push({
            type: 'pawn',
            from: fromSquare,
            to: toSquare,
            whiteToMove: gameState.whiteToMove
        });

        // End turn
        gameState.whiteToMove = !gameState.whiteToMove;
        gameState.phase = 'bobail';
        gameState.selectedSquare = null;
        gameState.validMoves = [];
    }

    // Check for game over
    const result = checkGameOver();
    if (result.over) {
        gameState.gameOver = true;
        gameState.winner = result.winner;
    }

    renderBoard();
    updateUI();

    // AI move if needed
    if (!gameState.gameOver && shouldAIMove()) {
        setTimeout(makeAIMove, 500);
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
            const pawns = pawnMove.whiteToMove ? gameState.whitePawns : gameState.blackPawns;
            const idx = pawns.indexOf(pawnMove.to);
            pawns[idx] = pawnMove.from;

            // Undo bobail
            const bobailMove = gameState.moveHistory.pop();
            gameState.bobailSquare = bobailMove.from;

            gameState.whiteToMove = pawnMove.whiteToMove;
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
    if (gameState.playerColor === 'both') return false;
    if (gameState.playerColor === 'white' && !gameState.whiteToMove) return true;
    if (gameState.playerColor === 'black' && gameState.whiteToMove) return true;
    return false;
}

// ==================== AI with Alpha-Beta Search ====================

// Evaluation function - returns score from White's perspective
function evaluate(state) {
    const bobailRow = Math.floor(state.bobailSquare / BOARD_SIZE);

    // Terminal conditions
    if (bobailRow === 0) return 10000; // White wins
    if (bobailRow === BOARD_SIZE - 1) return -10000; // Black wins

    // Check if bobail is trapped
    const bobailMoves = getBobailMovesForState(state);
    if (bobailMoves.length === 0) {
        // Whoever's turn it is wins (they trapped the bobail)
        return state.whiteToMove ? 10000 : -10000;
    }

    let score = 0;

    // Bobail position - closer to opponent's home is better
    // White wants bobail on row 4 (score negative for white perspective since black wins there)
    // Actually: White wants bobail near row 0, Black wants near row 4
    // From White's perspective: bobail near row 0 is good (positive)
    score += (BOARD_SIZE - 1 - bobailRow) * 100; // Higher = bobail closer to White's home (row 0)

    // Bobail mobility
    score += bobailMoves.length * 10;

    // Pawn positioning - control center and block opponent
    for (const sq of state.whitePawns) {
        const [r, c] = toRowCol(sq);
        // Pawns closer to center columns are better
        score += (2 - Math.abs(c - 2)) * 5;
        // Pawns further from home row can push bobail
        score += r * 3;
    }

    for (const sq of state.blackPawns) {
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
    const occupied = new Set([...state.whitePawns, ...state.blackPawns, state.bobailSquare]);

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
    const occupied = new Set([...state.whitePawns, ...state.blackPawns, state.bobailSquare]);

    for (const [dr, dc] of DIRECTIONS) {
        let newRow = row + dr;
        let newCol = col + dc;

        while (isValidSquare(newRow, newCol)) {
            const newSq = toSquare(newRow, newCol);
            if (occupied.has(newSq)) break;
            moves.push(newSq);
            newRow += dr;
            newCol += dc;
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

        const pawns = state.whiteToMove ? state.whitePawns : state.blackPawns;

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
    const newWhitePawns = [...state.whitePawns];
    const newBlackPawns = [...state.blackPawns];

    if (state.whiteToMove) {
        newWhitePawns[move.pawnIndex] = move.pawnTo;
    } else {
        newBlackPawns[move.pawnIndex] = move.pawnTo;
    }

    return {
        whitePawns: newWhitePawns,
        blackPawns: newBlackPawns,
        bobailSquare: move.bobailTo,
        whiteToMove: !state.whiteToMove
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

// AI makes a move using alpha-beta search
function makeAIMove() {
    if (gameState.gameOver) return;

    // Convert gameState to search state
    const state = {
        whitePawns: [...gameState.whitePawns],
        blackPawns: [...gameState.blackPawns],
        bobailSquare: gameState.bobailSquare,
        whiteToMove: gameState.whiteToMove
    };

    // Search depth (3-4 is reasonable for responsiveness)
    const depth = 3;
    const maximizing = state.whiteToMove;

    const result = alphaBeta(state, depth, -Infinity, Infinity, maximizing);

    if (result.move) {
        // Make bobail move
        if (gameState.phase === 'bobail') {
            makeMove(result.move.bobailTo);
        }

        // Make pawn move (after short delay for visual feedback)
        setTimeout(() => {
            if (gameState.phase === 'pawn' && !gameState.gameOver) {
                gameState.selectedSquare = result.move.pawnFrom;
                makeMove(result.move.pawnTo);
            }
        }, 300);
    }
}

// Handle square click
function handleSquareClick(sq) {
    if (gameState.gameOver) return;
    if (shouldAIMove()) return; // Not player's turn

    const piece = getPieceAt(sq);

    if (gameState.phase === 'bobail') {
        // Bobail phase - can only move bobail
        if (piece === 'bobail') {
            // Select bobail
            gameState.selectedSquare = sq;
            gameState.validMoves = getBobailMoves();
            renderBoard();
        } else if (gameState.validMoves.includes(sq)) {
            // Move bobail to this square
            makeMove(sq);
        }
    } else {
        // Pawn phase
        const myPawns = gameState.whiteToMove ? gameState.whitePawns : gameState.blackPawns;

        if (myPawns.includes(sq)) {
            // Select this pawn
            gameState.selectedSquare = sq;
            gameState.validMoves = getPawnMoves(sq);
            renderBoard();
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

    for (let row = 0; row < BOARD_SIZE; row++) {
        for (let col = 0; col < BOARD_SIZE; col++) {
            const sq = toSquare(row, col);
            const square = document.createElement('div');
            square.className = 'square';
            square.classList.add((row + col) % 2 === 0 ? 'light' : 'dark');
            square.dataset.square = sq;

            // Highlight selected square
            if (sq === gameState.selectedSquare) {
                square.classList.add('selected');
            }

            // Show valid moves
            if (gameState.validMoves.includes(sq)) {
                square.classList.add('valid-move');
                if (isOccupied(sq)) {
                    square.classList.add('has-piece');
                }
            }

            // Add hints if enabled
            if (gameState.hintsEnabled && !gameState.gameOver) {
                const hint = getMoveHint(sq);
                if (hint) {
                    square.classList.add(`hint-${hint}`);
                }
            }

            // Add piece if present
            const piece = getPieceAt(sq);
            if (piece) {
                const pieceEl = document.createElement('div');
                pieceEl.className = `piece ${piece}`;
                square.appendChild(pieceEl);
            }

            square.addEventListener('click', () => handleSquareClick(sq));
            board.appendChild(square);
        }
    }
}

// Get hint for a square (placeholder - will use solved database)
function getMoveHint(sq) {
    if (!gameState.validMoves.includes(sq)) return null;

    // Placeholder: random hints for now
    // Later: look up position in opening book
    const hints = ['win', 'draw', 'loss'];
    return hints[Math.floor(Math.random() * hints.length)];
}

// Update UI elements
function updateUI() {
    // Turn indicator
    const turnText = document.getElementById('turn-text');
    const turnDot = document.querySelector('.turn-dot');

    if (gameState.gameOver) {
        turnText.textContent = `${gameState.winner === 'white' ? 'White' : 'Black'} wins!`;
    } else {
        const phaseText = gameState.phase === 'bobail' ? 'Move Bobail' : 'Move Pawn';
        turnText.textContent = `${gameState.whiteToMove ? 'White' : 'Black'}: ${phaseText}`;
    }

    turnDot.className = 'turn-dot';
    turnDot.classList.add(gameState.whiteToMove ? 'white-dot' : 'black-dot');

    // Evaluation (placeholder)
    const evalValue = document.getElementById('eval-value');
    evalValue.textContent = '--';
    evalValue.className = 'eval-value';

    // Hint legend visibility
    const hintLegend = document.getElementById('hint-legend');
    hintLegend.classList.toggle('visible', gameState.hintsEnabled);

    // Move history
    updateMoveHistory();

    // Show game over overlay if needed
    if (gameState.gameOver) {
        showGameOverOverlay();
    }
}

// Update move history display
function updateMoveHistory() {
    const moveList = document.getElementById('move-list');
    moveList.innerHTML = '';

    let moveNum = 1;
    for (let i = 0; i < gameState.moveHistory.length; i += 2) {
        const bobailMove = gameState.moveHistory[i];
        const pawnMove = gameState.moveHistory[i + 1];

        const entry = document.createElement('div');
        entry.className = 'move-entry';

        const color = bobailMove.whiteToMove ? 'W' : 'B';
        let text = `<span class="move-number">${moveNum}.</span>${color}: `;
        text += `B${squareToNotation(bobailMove.from)}-${squareToNotation(bobailMove.to)}`;

        if (pawnMove) {
            text += `, ${squareToNotation(pawnMove.from)}-${squareToNotation(pawnMove.to)}`;
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
                <button class="btn" onclick="hideGameOverOverlay(); initGame();">Play Again</button>
            </div>
        `;
        document.body.appendChild(overlay);
    }

    const title = document.getElementById('game-over-title');
    const message = document.getElementById('game-over-message');

    title.textContent = `${gameState.winner === 'white' ? 'White' : 'Black'} Wins!`;

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

// Event listeners
document.addEventListener('DOMContentLoaded', () => {
    // Initialize game
    initGame();

    // Hint toggle
    document.getElementById('hint-toggle').addEventListener('click', (e) => {
        gameState.hintsEnabled = !gameState.hintsEnabled;
        e.target.classList.toggle('active', gameState.hintsEnabled);
        e.target.innerHTML = gameState.hintsEnabled
            ? '<span class="hint-icon">💡</span> Hide Hints'
            : '<span class="hint-icon">💡</span> Show Hints';
        renderBoard();
        updateUI();
    });

    // New game
    document.getElementById('new-game').addEventListener('click', () => {
        hideGameOverOverlay();
        initGame();
    });

    // Undo
    document.getElementById('undo').addEventListener('click', undoMove);

    // Player color select
    document.getElementById('player-color').addEventListener('change', (e) => {
        gameState.playerColor = e.target.value;
        initGame();
    });
});
