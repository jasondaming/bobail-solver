#!/usr/bin/env python3
"""
Simple HTTP server that wraps the lookup tool for the web UI.
Run this locally to play against the perfect solver.

Usage:
    python lookup_server.py --db /path/to/solver_db --rules official
    python lookup_server.py --db /path/to/solver_db --rules flexible

Then update game.js to fetch from http://localhost:8080/lookup?pos=WP,BP,BOB,STM
"""

import subprocess
import json
import argparse
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import os

# Global config
LOOKUP_PATH = "./build/lookup"
DB_PATH = ""
RULES = "official"
USE_PNS = False
PNS_LOOKUP_PATH = "./build/pns_lookup"
PNS_CHECKPOINT = ""

class LookupHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        # Add CORS headers
        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()

        parsed = urlparse(self.path)

        if parsed.path == '/lookup':
            params = parse_qs(parsed.query)
            pos = params.get('pos', [''])[0]

            if not pos:
                self.wfile.write(json.dumps({"error": "Missing pos parameter"}).encode())
                return

            # Call the lookup tool
            try:
                if USE_PNS:
                    # Use PNS checkpoint lookup
                    result = subprocess.run(
                        [PNS_LOOKUP_PATH, "--checkpoint", PNS_CHECKPOINT, "--query", pos, "--json"],
                        capture_output=True,
                        text=True,
                        timeout=5
                    )
                    # PNS lookup returns JSON directly
                    try:
                        response = json.loads(result.stdout)
                        self.wfile.write(json.dumps(response).encode())
                        return
                    except json.JSONDecodeError:
                        self.wfile.write(json.dumps({"error": "Parse error", "raw": result.stdout}).encode())
                        return

                rules_flag = "--official" if RULES == "official" else "--flexible"
                result = subprocess.run(
                    [LOOKUP_PATH, "--db", DB_PATH, rules_flag, "--query", pos],
                    capture_output=True,
                    text=True,
                    timeout=5
                )

                # Parse the output
                output = result.stdout + result.stderr
                response = self.parse_lookup_output(output, pos)
                self.wfile.write(json.dumps(response).encode())

            except subprocess.TimeoutExpired:
                self.wfile.write(json.dumps({"error": "Timeout"}).encode())
            except Exception as e:
                self.wfile.write(json.dumps({"error": str(e)}).encode())

        elif parsed.path == '/health':
            self.wfile.write(json.dumps({"status": "ok", "rules": RULES}).encode())

        else:
            self.wfile.write(json.dumps({"error": "Unknown endpoint"}).encode())

    def do_OPTIONS(self):
        # Handle CORS preflight
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def parse_lookup_output(self, output, pos):
        """Parse lookup tool output into JSON response"""
        lines = output.strip().split('\n')

        result = {
            "pos": pos,
            "result": "unknown",
            "best_move": None,
            "all_moves": []
        }

        for line in lines:
            line = line.strip()

            if line.startswith("Result:"):
                if "WIN" in line:
                    result["result"] = "win"
                elif "LOSS" in line:
                    result["result"] = "loss"
                elif "DRAW" in line:
                    result["result"] = "draw"

            elif line.startswith("Best move:"):
                # Parse "B->7 P:0->15"
                move_str = line.replace("Best move:", "").strip()
                result["best_move"] = self.parse_move(move_str)

            elif line.startswith("B->") and "->" in line:
                # Parse move line like "  B->7 P:0->15 -> DRAW *"
                parts = line.split("->")
                if len(parts) >= 3:
                    move_str = "->".join(parts[:2]).strip()  # "B->7 P:0->15"
                    eval_part = parts[-1].strip()  # "DRAW *" or "WIN" etc

                    move = self.parse_move(move_str)
                    if move:
                        eval_str = eval_part.replace("*", "").strip().lower()
                        move["eval"] = eval_str
                        move["best"] = "*" in eval_part
                        result["all_moves"].append(move)

        return result

    def parse_move(self, move_str):
        """Parse move string like 'B->7 P:0->15' into dict"""
        try:
            # Format: "B->BOB P:FROM->TO"
            parts = move_str.split()
            if len(parts) != 2:
                return None

            bob_part = parts[0]  # "B->7"
            pawn_part = parts[1]  # "P:0->15"

            bob_to = int(bob_part.split("->")[1])
            pawn_parts = pawn_part.replace("P:", "").split("->")
            pawn_from = int(pawn_parts[0])
            pawn_to = int(pawn_parts[1])

            return {
                "bobail_to": bob_to,
                "pawn_from": pawn_from,
                "pawn_to": pawn_to
            }
        except:
            return None

    def log_message(self, format, *args):
        # Quieter logging
        pass

def main():
    global DB_PATH, RULES, LOOKUP_PATH, USE_PNS, PNS_LOOKUP_PATH, PNS_CHECKPOINT

    parser = argparse.ArgumentParser(description='Bobail lookup server')
    parser.add_argument('--db', help='Path to solver database (for retrograde mode)')
    parser.add_argument('--rules', choices=['official', 'flexible'], default='official')
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--lookup', default='./build/lookup', help='Path to lookup binary')
    parser.add_argument('--pns', help='Path to PNS checkpoint (enables PNS mode)')
    parser.add_argument('--pns-lookup', default='./build/pns_lookup', help='Path to pns_lookup binary')
    args = parser.parse_args()

    if args.pns:
        USE_PNS = True
        PNS_CHECKPOINT = args.pns
        PNS_LOOKUP_PATH = args.pns_lookup
        print(f"Starting Bobail PNS lookup server...")
        print(f"  PNS Checkpoint: {PNS_CHECKPOINT}")
    elif args.db:
        DB_PATH = args.db
        RULES = args.rules
        LOOKUP_PATH = args.lookup
        print(f"Starting Bobail retrograde lookup server...")
        print(f"  Database: {DB_PATH}")
        print(f"  Rules: {RULES}")
    else:
        print("Error: Must specify either --db or --pns")
        return

    print(f"  Port: {args.port}")
    print(f"\nTest URL: http://localhost:{args.port}/lookup?pos=1f,1f00000,12,1")
    print("Press Ctrl+C to stop\n")

    server = HTTPServer(('', args.port), LookupHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()

if __name__ == '__main__':
    main()
