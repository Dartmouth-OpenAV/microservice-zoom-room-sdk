#!/bin/bash

# the purpose of this script is to run a binary that would normally be run in interactive mode
# and instead attach a fifo for its stdin so commands can be sent to it

set -e

if [ -z "$1" -o -z "$2" -o -z "$3" ]
then
    $0 /path/to/binary parameter /path/to/fifo
    exit 1
fi

if [ ! -x "$1" ]
then
    echo "$1 is missing or not executable"
    exit 1
fi

if [ -e "$3.fifo" ]
then
    "$3.fifo already exists"
    exit 1
fi

if [ -e "$3.queue" ]
then
    "$3.queue already exists"
    exit 1
fi

BINARY=$1
PARAMETER=$2
FIFOQUEUE=$3

cleanup() {
    echo "Cleaning up..."
    pkill -TERM -P $$ || true
    rm -f "$FIFOQUEUE.fifo" "$FIFOQUEUE.queue"
}
trap cleanup EXIT

# setup
rm -f "$FIFOQUEUE.fifo" "$FIFOQUEUE.queue"
mkfifo "$FIFOQUEUE.fifo"
touch "$FIFOQUEUE.queue"

# start the tailer that feeds the fifo
tail -f "$FIFOQUEUE.queue" > "$FIFOQUEUE.fifo" &
TAIL_PID=$!

# start the binary with fifo as stdin
$BINARY $PARAMETER < "$FIFOQUEUE.fifo" &

# loop to not exit
while true; do
    sleep 1
done