#!/bin/bash
# Integration test script for RDO Blue sensor with Payload SDK

echo "=========================================="
echo "RDO Blue + Payload SDK Integration Test"
echo "=========================================="
echo ""

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_READER="$SCRIPT_DIR/pruebas.py"
PAYLOAD_BIN="$SCRIPT_DIR/build/bin/dji_sdk_demo_on_rpi_cxx"
SHARED_DATA_FILE="/tmp/rdo_sensor_data.json"

# Check if Python script exists
if [ ! -f "$PYTHON_READER" ]; then
    echo -e "${RED}Error: Python reader script not found at $PYTHON_READER${NC}"
    exit 1
fi

# Check if Payload SDK binary exists
if [ ! -f "$PAYLOAD_BIN" ]; then
    echo -e "${RED}Error: Payload SDK binary not found at $PAYLOAD_BIN${NC}"
    exit 1
fi

echo -e "${YELLOW}1. Cleaning up previous data file...${NC}"
rm -f "$SHARED_DATA_FILE"
echo "   OK"
echo ""

echo -e "${YELLOW}2. Starting Python RDO Blue reader in background...${NC}"
python3 "$PYTHON_READER" > /tmp/rdo_reader.log 2>&1 &
READER_PID=$!
echo "   Reader PID: $READER_PID"

# Wait for data file to be created
echo -e "${YELLOW}3. Waiting for sensor data to be available...${NC}"
for i in {1..10}; do
    if [ -f "$SHARED_DATA_FILE" ]; then
        sleep 0.5
        echo -e "   ${GREEN}✓ Data file created${NC}"
        echo "   Content:"
        cat "$SHARED_DATA_FILE" | sed 's/^/      /'
        break
    fi
    echo -n "."
    sleep 1
    if [ $i -eq 10 ]; then
        echo -e "${RED}✗ Timeout waiting for data file${NC}"
        kill $READER_PID
        exit 1
    fi
done
echo ""

echo -e "${YELLOW}4. Testing JSON parsing from C code...${NC}"
echo "   (This would be tested when running the actual Payload SDK)"
echo "   Expected JSON format:"
echo "   {\"type\":\"sensor\",\"temp\":XX.XX,\"oxi\":XX.XX,\"sat\":XX.XX,\"pp\":XX.XX,\"ts\":XXXXX}"
echo ""

echo -e "${YELLOW}5. Reader process status:${NC}"
if kill -0 $READER_PID 2>/dev/null; then
    echo -e "   ${GREEN}✓ Reader is still running${NC}"
    echo "   You can verify continuous updates:"
    echo "   $ watch -n 1 'cat /tmp/rdo_sensor_data.json'"
else
    echo -e "   ${RED}✗ Reader process stopped${NC}"
fi
echo ""

echo -e "${YELLOW}6. Data file updates monitored:${NC}"
for i in {1..3}; do
    sleep 1
    echo "   Update $i:"
    cat "$SHARED_DATA_FILE" | sed 's/^/      /'
done
echo ""

echo -e "${YELLOW}7. Cleanup and summary:${NC}"
echo "   Python reader PID: $READER_PID"
echo "   To stop the reader: kill $READER_PID"
echo ""
echo -e "${GREEN}✓ Integration test complete!${NC}"
echo ""
echo "Next steps:"
echo "1. Run the Payload SDK demo with your aircraft:"
echo "   $PAYLOAD_BIN"
echo ""
echo "2. Keep the Python reader running in background:"
echo "   python3 $PYTHON_READER &"
echo ""
echo "3. Data will be transmitted to RC/Mobile app via Payload SDK"
echo ""

# Keep reader running or stop it based on user input
read -p "Keep reader running? (y/n): " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    kill $READER_PID
    echo "Reader stopped."
else
    echo "Reader will continue running. Process ID: $READER_PID"
    echo "You can stop it later with: kill $READER_PID"
fi
