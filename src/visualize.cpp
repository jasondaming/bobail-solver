// Generate web app URLs for position visualization
// Usage: visualize [options]
//   --starting    Show starting position URL
//   --first-moves Show URLs after each legal first move
//   --random N    Generate N random positions from PNS data
//   --position WP,BP,BOB,STM  Show URL for specific position

#include "board.h"
#include "movegen.h"
#include "hash.h"
#include "symmetry.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <random>

namespace bobail {

// Convert position to web app URL format
std::string to_web_url(const State& s, bool pawn_phase = false) {
    std::string url = "https://jasondaming.github.io/bobail-solver/?pos=";

    // Green pawns (white_pawns in our code)
    std::string gp;
    for (int sq = 0; sq < 25; ++sq) {
        if (s.white_pawns & (1u << sq)) {
            if (sq < 10) gp += ('0' + sq);
            else gp += ('a' + (sq - 10));
        }
    }

    // Red pawns (black_pawns in our code)
    std::string rp;
    for (int sq = 0; sq < 25; ++sq) {
        if (s.black_pawns & (1u << sq)) {
            if (sq < 10) rp += ('0' + sq);
            else rp += ('a' + (sq - 10));
        }
    }

    // Bobail position
    char bob = s.bobail_sq < 10 ? ('0' + s.bobail_sq) : ('a' + (s.bobail_sq - 10));

    // Turn and phase
    char turn = s.white_to_move ? 'w' : 'b';
    char phase = pawn_phase ? 'P' : 'B';

    url += gp + "-" + rp + "-" + bob + turn + phase;
    return url;
}

void print_board(const State& s) {
    std::cout << "  0 1 2 3 4\n";
    for (int r = 0; r < 5; ++r) {
        std::cout << r << " ";
        for (int c = 0; c < 5; ++c) {
            int sq = r * 5 + c;
            if (s.bobail_sq == sq) std::cout << "B ";
            else if (s.white_pawns & (1u << sq)) std::cout << "G ";
            else if (s.black_pawns & (1u << sq)) std::cout << "R ";
            else std::cout << ". ";
        }
        std::cout << "\n";
    }
    std::cout << (s.white_to_move ? "Green" : "Red") << " to move\n";
}

} // namespace bobail

int main(int argc, char* argv[]) {
    bobail::init_move_tables();
    bobail::init_zobrist();
    bobail::init_symmetry();

    bool show_starting = false;
    bool show_first_moves = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--starting") == 0) {
            show_starting = true;
        } else if (strcmp(argv[i], "--first-moves") == 0) {
            show_first_moves = true;
        }
    }

    // Default: show starting and first moves
    if (!show_starting && !show_first_moves) {
        show_starting = true;
        show_first_moves = true;
    }

    if (show_starting) {
        std::cout << "=== STARTING POSITION ===\n\n";
        bobail::State start = bobail::State::starting_position();
        bobail::print_board(start);
        std::cout << "\nFirst turn is PAWN ONLY (Green skips Bobail move)\n";
        std::cout << "\nURL: " << bobail::to_web_url(start, true) << "\n\n";
    }

    if (show_first_moves) {
        std::cout << "=== LEGAL FIRST MOVES (Pawn only, Bobail stays at center) ===\n\n";
        bobail::State start = bobail::State::starting_position();
        auto moves = bobail::generate_moves(start);

        std::cout << "Total legal first moves: " << moves.size() << "\n\n";

        int num = 1;
        for (const auto& m : moves) {
            bobail::State after = bobail::apply_move(start, m);

            std::cout << num++ << ". Green plays: P:" << (int)m.pawn_from << "->" << (int)m.pawn_to << "\n";
            bobail::print_board(after);
            // After Green's pawn move, it's Red's turn in Bobail phase
            std::cout << "URL: " << bobail::to_web_url(after, false) << "\n\n";
        }
    }

    return 0;
}
