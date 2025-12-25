#!/bin/bash
# Monitor solver and auto-start export when complete

echo "Monitoring solver for completion..."

while true; do
    # Check if solver is still running
    if ! pgrep -f "retrograde_db.*--official" > /dev/null; then
        echo "Solver process ended, checking if complete..."
        
        # Check if it completed successfully by looking for COMPLETE in log
        if grep -q "SOLUTION COMPLETE" /workspace/solver_output.log; then
            echo "Solver completed successfully! Starting Official export..."
            ./build/export_book --db /workspace/solver_db --output /workspace/opening_book_official.json --official --depth 20 > /workspace/export_official.log 2>&1
            echo "Official export finished!"
            exit 0
        else
            echo "Solver ended but did not complete successfully"
            tail -20 /workspace/solver_output.log
            exit 1
        fi
    fi
    
    # Sleep 30 seconds between checks
    sleep 30
done
