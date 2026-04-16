#!/bin/sh
set -eu

MONKEY_ID="${MONKEY_ID:?MONKEY_ID must be set}"
PORT="${TFTP_PORT:-$((23069 + MONKEY_ID))}"
LOG="/logs/cm${MONKEY_ID}.log"
RUN_COUNT=0
FAIL_COUNT=0

echo "$(date -Iseconds) [CM${MONKEY_ID}] === Soak test starting (port=${PORT}) ===" >> "$LOG"

while true; do
    RUN_COUNT=$((RUN_COUNT + 1))
    START=$(date -Iseconds)

    # Run the chaos monkey, capturing output
    set +e
    OUTPUT=$(python3 "scripts/chaos_monkey${MONKEY_ID}.py" \
        --port "$PORT" \
        --server-bin ./build/release/tftptest 2>&1)
    RC=$?
    set -e

    if [ $RC -ne 0 ]; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
        echo "${START} [CM${MONKEY_ID}] FAIL #${FAIL_COUNT} (run=${RUN_COUNT}, exit=${RC})" >> "$LOG"
        echo "${OUTPUT}" >> "$LOG"
        echo "---" >> "$LOG"
    else
        # Compact pass line - don't dump full output for successes
        echo "${START} [CM${MONKEY_ID}] PASS (run=${RUN_COUNT}, fails=${FAIL_COUNT})" >> "$LOG"
    fi

    sleep 2
done
