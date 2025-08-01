#!/bin/bash
set -e

DOCKER="docker run --rm --platform=linux/amd64 -v $PWD:/project -w /project -e HOME=/tmp voasd00/runable-idf:latest"

echo "🧹 Cleaning..."
$DOCKER idf.py fullclean

echo "🛠️  Building..."
$DOCKER idf.py build

echo "BUILD DONE."