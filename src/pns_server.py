#!/usr/bin/env python3
"""
HTTP server that provides hints from PNS checkpoint data.
This replaces lookup_server.py when using PNS data instead of retrograde DB.

Usage:
    python pns_server.py --checkpoint /path/to/pns_checkpoint.bin --port 8081

The web UI will call http://localhost:8081/lookup?pos=WP,BP,BOB,STM
"""

import struct
import json
import argparse
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import os
import sys

# PNS data loaded in memory
pns_table = {}
stats = {}

def load_checkpoint(path):
    """Load PNS checkpoint file into memory"""
    global pns_table, stats

    print(f"Loading checkpoint: {path}")

    with open(path, 'rb') as f:
        # Read header
        magic = struct.unpack('<Q', f.read(8))[0]
        if magic != 0x504E5343484B5054:  # "PNSCHKPT"
            print("Invalid checkpoint magic")
            return False

        version = struct.unpack('<Q', f.read(8))[0]
        num_entries = struct.unpack('<Q', f.read(8))[0]
        nodes_searched = struct.unpack('<Q', f.read(8))[0]
        nodes_proved = struct.unpack('<Q', f.read(8))[0]
        nodes_disproved = struct.unpack('<Q', f.read(8))[0]
        retro_hits = struct.unpack('<Q', f.read(8))[0]

        stats['entries'] = num_entries
        stats['nodes_searched'] = nodes_searched
        stats['proved'] = nodes_proved
        stats['disproved'] = nodes_disproved

        print(f"  Entries: {num_entries}")
        print(f"  Proved: {nodes_proved}")
        print(f"  Disproved: {nodes_disproved}")

        # Read entries
        # Each entry: hash(8) + proof(4) + disproof(4) + result(1) = 17 bytes
        # But struct packing likely makes it 24 bytes
        entry_size = 17  # Try packed size first

        pns_table.clear()
        for i in range(num_entries):
            data = f.read(24)  # Padded to 24 bytes due to alignment
            if len(data) < 17:
                break

            hash_val = struct.unpack('<Q', data[0:8])[0]
            proof = struct.unpack('<I', data[8:12])[0]
            disproof = struct.unpack('<I', data[12:16])[0]
            result = data[16]

            pns_table[hash_val] = {
                'proof': proof,
                'disproof': disproof,
                'result': result  # 0=unknown, 1=win, 2=loss, 3=draw
            }

            if (i + 1) % 1000000 == 0:
                print(f"  Loaded {(i+1)//1000000}M entries...")

        print(f"Loaded {len(pns_table)} entries into memory")
        return True

def pos_to_hash(pos_str):
    """
    Convert position string (WP,BP,BOB,STM) to canonical hash.
    This is a simplified version - for full accuracy we'd need the C++ hash function.
    For now, we'll use a simple hash that should match canonical positions.
    """
    try:
        parts = pos_str.split(',')
        wp = int(parts[0], 16)
        bp = int(parts[1], 16)
        bob = int(parts[2])
        stm = int(parts[3])

        # Simple hash combining all state info
        # Note: This won't match the C++ Zobrist hash exactly
        # We need to either:
        # 1. Use the same Zobrist tables
        # 2. Store position -> hash mapping
        # For now, return a placeholder
        return None  # Can't compute hash without Zobrist tables
    except:
        return None

class PNSHandler(BaseHTTPRequestHandler):
    def do_GET(self):
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

            # For now, return unknown since we can't compute hash
            # The real solution is to have the lookup binary query PNS data
            response = {
                "pos": pos,
                "result": "unknown",
                "note": "PNS lookup requires hash computation - use pns_lookup binary"
            }
            self.wfile.write(json.dumps(response).encode())

        elif parsed.path == '/stats':
            self.wfile.write(json.dumps(stats).encode())

        elif parsed.path == '/health':
            self.wfile.write(json.dumps({
                "status": "ok",
                "source": "pns",
                "entries": len(pns_table)
            }).encode())

        else:
            self.wfile.write(json.dumps({"error": "Unknown endpoint"}).encode())

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def log_message(self, format, *args):
        pass

def main():
    parser = argparse.ArgumentParser(description='Bobail PNS lookup server')
    parser.add_argument('--checkpoint', required=True, help='Path to PNS checkpoint file')
    parser.add_argument('--port', type=int, default=8081)
    args = parser.parse_args()

    if not os.path.exists(args.checkpoint):
        print(f"Checkpoint not found: {args.checkpoint}")
        sys.exit(1)

    if not load_checkpoint(args.checkpoint):
        sys.exit(1)

    print(f"\nStarting PNS server on port {args.port}")
    print(f"Note: Direct hash lookup not yet implemented")
    print(f"      Use pns_lookup binary for accurate lookups")
    print(f"\nTest: http://localhost:{args.port}/stats")
    print("Press Ctrl+C to stop\n")

    server = HTTPServer(('', args.port), PNSHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        server.shutdown()

if __name__ == '__main__':
    main()
