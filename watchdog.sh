#!/bin/bash
# PNS Solver Watchdog Script
# Monitors the pns_enhanced process and restarts it if it dies

# Note: Depth limit is now built into the solver, no need for ulimit -s unlimited
SOLVER_CMD="/workspace/src/build_linux/pns_enhanced --db /workspace/solver_db_official --checkpoint /workspace/pns_checkpoint.bin --interval 300 --resume"
LOG_FILE="/workspace/pns_output.log"
WATCHDOG_LOG="/workspace/watchdog.log"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$WATCHDOG_LOG"
}

check_and_restart() {
    if ! pgrep -f "pns_enhanced" > /dev/null; then
        log "PNS solver not running - restarting..."

        # Check exit status of last run if possible
        if [ -f /tmp/pns_exit_code ]; then
            EXIT_CODE=$(cat /tmp/pns_exit_code)
            log "Previous exit code: $EXIT_CODE"
        fi

        # Restart the solver
        cd /workspace
        $SOLVER_CMD >> "$LOG_FILE" 2>&1 &
        PID=$!
        echo $PID > /tmp/pns_pid
        log "Restarted PNS solver with PID $PID"

        # Wait a moment and verify it started
        sleep 2
        if pgrep -f "pns_enhanced" > /dev/null; then
            log "PNS solver started successfully"
        else
            log "ERROR: PNS solver failed to start!"
            wait $PID
            echo $? > /tmp/pns_exit_code
        fi
    fi
}

log "Watchdog started"

# Initial check
check_and_restart

# Monitor loop - check every 30 seconds
while true; do
    sleep 30
    check_and_restart
done
