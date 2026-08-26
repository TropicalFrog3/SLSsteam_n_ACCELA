#!/bin/bash

DOCKER_HOST="sls-host"

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
cd "$SCRIPT_DIR"

if [ -z "$TRACE" ]; then
	TRACE=0
fi

if [ -z "$DEBUG" ]; then
	DEBUG=0
fi

if [ -z "$NATIVE" ]; then
	NATIVE=0
fi

docker build -t $DOCKER_HOST .
docker run \
	--name $DOCKER_HOST \
	-e "TRACE=$TRACE" \
	-e "DEBUG=$DEBUG" \
	-e "NATIVE=$NATIVE" \
	--user=sls \
	--rm \
	--mount=type=bind,source=../,target=/src \
	--workdir=/src \
	sls-host:latest \
	make $@
