#!/bin/bash
set -e

PROJECT_DIR="/home/hpcn/Desktop/PSDK/psdk-3.14.1-fc30"
SERVICE_NAME="drone-control.service"

cp "$PROJECT_DIR/deploy/start_drone.sh" "$PROJECT_DIR/start_drone.sh"
chmod +x "$PROJECT_DIR/start_drone.sh"

sudo cp "$PROJECT_DIR/deploy/$SERVICE_NAME" "/etc/systemd/system/$SERVICE_NAME"
if [ ! -f /etc/default/drone-control ]; then
    sudo cp "$PROJECT_DIR/deploy/drone-control.env" /etc/default/drone-control
fi
sudo systemctl daemon-reload
sudo systemctl enable "$SERVICE_NAME"
sudo systemctl restart "$SERVICE_NAME"

systemctl status "$SERVICE_NAME" --no-pager
