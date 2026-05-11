#!/bin/sh

set -e

cd "$(dirname "$0")"

IMAGE_NAME="os-course-dev:latest"

docker run \
    -it \
    --rm \
    --volume $(pwd)/../..:/xv6 "$IMAGE_NAME" \
    /bin/bash
